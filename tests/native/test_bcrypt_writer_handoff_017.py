"""Finite toy-probe checks; real temporary files, no native children or host scan."""
import builtins
import json
from pathlib import Path
import stat
from types import SimpleNamespace
import tempfile
import unittest
from unittest import mock

import check_bcrypt_writer_handoff_017 as h
from bcrypt_guarded_owner_017 import Refused

PROBES = [[9, 10, 20, stat.S_IFREG], [12, 10, 21, stat.S_IFDIR]]


def reply(inherited):
    return h.encode(dict(status='owner_complete_unreviewed', binding=dict(request_sha256=h.REQUEST),
                         synthetic=True, inherited_matches=[inherited, inherited], observation_accepted=False))


class WriterHandoffTests(unittest.TestCase):
    def execute_probe(self, fstat):
        output=[]
        fake=SimpleNamespace(fstat=fstat, write=lambda fd,raw:(output.append(raw) or len(raw)))
        def imports(name, *args, **kwargs):
            return fake if name=='os' else builtins.__import__(name,*args,**kwargs)
        scope={'__builtins__':dict(vars(builtins),__import__=imports)}
        exec(compile(h.probe_source('/synthetic/python',PROBES),'synthetic_probe','exec'),scope)
        self.assertEqual(len(output),1)
        return output[0]

    def test_counterexample_matches_exact_file_and_directory(self):
        lookup={fd:SimpleNamespace(st_dev=dev,st_ino=ino,st_mode=mode|0o600)
                for fd,dev,ino,mode in PROBES}
        raw=self.execute_probe(lookup.__getitem__)
        self.assertEqual(h.check_probe(raw,True)['inherited_matches'],[True,True])
        with self.assertRaises(Refused):h.check_probe(raw,False)

    def test_closed_descriptors_are_not_inherited(self):
        def closed(_):raise OSError('closed')
        raw=self.execute_probe(closed)
        h.check_probe(raw,False)
        with self.assertRaises(Refused):h.check_probe(raw,True)

    def test_reused_fd_numbers_do_not_count_as_same_open_file(self):
        raw=self.execute_probe(lambda _:SimpleNamespace(st_dev=10,st_ino=500,st_mode=stat.S_IFREG))
        h.check_probe(raw,False)

    def test_wrong_kind_is_not_the_original_directory(self):
        lookup={fd:SimpleNamespace(st_dev=dev,st_ino=ino,st_mode=stat.S_IFCHR)
                for fd,dev,ino,mode in PROBES}
        h.check_probe(self.execute_probe(lookup.__getitem__),False)

    def test_non_boolean_qualification_and_matches_refused(self):
        for key,value in [('synthetic',1),('observation_accepted',0),('inherited_matches',[0,0]),
                          ('status','passed'),('binding',dict(request_sha256='0'*64))]:
            r=h.decode(reply(False));r[key]=value
            with self.subTest(key=key),self.assertRaises(Refused):h.check_probe(h.encode(r),False)
        with self.assertRaises(Refused):h.check_probe(reply(False),0)

    def test_malformed_probe_inputs_rejected(self):
        for probes in ([],PROBES*2,[[1,2,3,stat.S_IFREG],PROBES[1]],
                       [[True,2,3,stat.S_IFREG],PROBES[1]],[PROBES[0],PROBES[0]]):
            with self.assertRaises(Refused):h.probe_source('/synthetic/python',probes)
        for python in ('relative','/python\nexecute','/bin/env python',False):
            with self.assertRaises(Refused):h.probe_source(python,PROBES)

    def test_probe_only_inspects_two_declared_descriptors(self):
        seen=[]
        def fstat(fd):seen.append(fd);raise OSError()
        self.execute_probe(fstat)
        self.assertEqual(seen,[9,12])
        source=h.probe_source('/synthetic/python',PROBES)
        self.assertTrue(source.startswith(b'#!/synthetic/python -S\n'))
        self.assertNotIn(b'/proc',source)
        self.assertNotIn(b'open(',source)

    def test_known_child_timeout_kills_once_then_waits(self):
        with tempfile.TemporaryDirectory() as directory:
            child=mock.Mock();child.wait.side_effect=[h.subprocess.TimeoutExpired('toy',3),0]
            child.poll.return_value=None
            with mock.patch.object(h.subprocess,'Popen',return_value=child) as popen:
                with self.assertRaises(h.subprocess.TimeoutExpired):
                    h.direct(Path(directory),'timeout',['/synthetic/python'],close_fds=True)
            self.assertEqual(popen.call_count,1)
            self.assertEqual(child.wait.call_count,2);child.kill.assert_called_once()
            self.assertTrue(popen.call_args.kwargs['close_fds'])
            self.assertFalse(popen.call_args.kwargs['shell'])
            self.assertEqual(popen.call_args.kwargs['env'],h.ENV)

    def test_native_close_and_reap_on_capture_refusal(self):
        with tempfile.TemporaryDirectory() as directory:
            backend=mock.Mock();child=backend.launch.return_value
            child.wait.return_value=0;child.poll.return_value=0
            child.capture.return_value=(reply(True),b'')
            with mock.patch.object(h,'PreparedNativeBackend',return_value=backend):
                with self.assertRaises(Refused):h.native(Path(directory),Path('/synthetic/probe'))
            backend.launch.assert_called_once_with(h.TARGET)
            child.kill.assert_not_called();child.close.assert_called_once();backend.close.assert_called_once()

    def test_native_cleanup_waits_even_when_signal_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            backend=mock.Mock();child=backend.launch.return_value
            child.wait.side_effect=[h.subprocess.TimeoutExpired('toy',3),0]
            child.poll.return_value=None;child.kill.side_effect=OSError('signal')
            with mock.patch.object(h,'PreparedNativeBackend',return_value=backend):
                with self.assertRaises(OSError):h.native(Path(directory),Path('/synthetic/probe'))
            self.assertEqual(child.wait.call_count,2)
            child.kill.assert_called_once();child.close.assert_called_once();backend.close.assert_called_once()

    def test_failed_launch_closes_owned_backend_without_relaunch(self):
        with tempfile.TemporaryDirectory() as directory:
            backend=mock.Mock();backend.launch.side_effect=OSError('launch')
            with mock.patch.object(h,'PreparedNativeBackend',return_value=backend):
                with self.assertRaises(OSError):h.native(Path(directory),Path('/synthetic/probe'))
            backend.launch.assert_called_once();backend.close.assert_called_once()

    def test_saved_records_are_exclusive_and_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);h.save(root,'one.json',{'first':True})
            original=(root/'one.json').read_bytes()
            with self.assertRaises(FileExistsError):h.save(root,'one.json',{'first':False})
            self.assertEqual((root/'one.json').read_bytes(),original)
            (root/'small').write_bytes(b'x'*h.CAP)
            self.assertEqual(len(h.read_small(root/'small')),h.CAP)
            (root/'large').write_bytes(b'x'*(h.CAP+1))
            with self.assertRaises(Refused):h.read_small(root/'large')


if __name__=='__main__':unittest.main(verbosity=2)
