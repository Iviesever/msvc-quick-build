"""Pure/mocked protocol tests. No GNU, own maps or live scope is executed."""
from pathlib import Path
import ast
import copy
import hashlib
import types
import tempfile
import unittest
from unittest.mock import patch

import check_bcrypt_decoder_loading_017 as d

LIBS=['/lib64/ld-linux-x86-64.so.2','/lib/x86_64-linux-gnu/libc.so.6']


def debug_text():
    lines=['find library=libc.so.6 [0]; searching',' search cache=/etc/ld.so.cache',
           ' trying file=/lib/x86_64-linux-gnu/libc.so.6',*['calling init: '+p for p in LIBS],
           'initialize program: '+d.DECODER,'transferring control: '+d.DECODER,
           'calling fini:  [0]',*['calling fini: '+p+' [0]' for p in reversed(LIBS)]]
    return ('\n'.join('  42:\t'+s for s in lines)+'\n').encode()


def rows():
    return [dict(rva=d.RVA_START+i,length=1,raw_hex='90' if i<102 else 'c3',
                 assembly='nop' if i<102 else 'ret') for i in range(103)]


def graph():
    canonical=['/usr/lib/ld.so','/usr/lib/libc.so']
    return dict(roots={'decoder':d.DECODER},
        nodes=[dict(path=p,bytes=1,sha256='a'*64) for p in [d.DECODER,*canonical]],
        aliases=[dict(path=p,target=c) for p,c in zip(LIBS,canonical)],
        edges=[dict(source=d.DECODER,provider=c) for c in canonical])


class DecoderContracts(unittest.TestCase):
    def test_fixture_self_created_exact_geometry(self):
        self.assertEqual(d.FIXTURE,b'\x90'*102+b'\xc3')
        self.assertEqual(len(d.FIXTURE),103)
        self.assertEqual(d.FIXTURE_SHA,hashlib.sha256(d.FIXTURE).hexdigest())
        self.assertEqual(d.fixture_listing(rows()),rows())

    def test_real_legacy_parser_checks_manufactured_gnu_text(self):
        # This is constructed text, not a claim to have executed objdump.
        text=b'\ntarget.bin:     file format binary\n\n\nDisassembly of section .data:\n\n000000018001117c <.data>:\n'
        for i,r in enumerate(rows()):
            text+=(f"   {0x18001117c+i:x}:\t{r['raw_hex']}\t{r['assembly']}\n").encode()
        backend=d.PreparedNativeBackend('/synthetic/new',d.DECODER,d.FIXTURE_SHA,'d'*64,
            output_factory=d.OutputDirectory,encode_json=d.encode_json,parse_listing=d.parse_listing)
        self.assertEqual(d.fixture_listing(backend.parse(text,d.FIXTURE)),rows())

    def test_wrong_disassembly_rejected(self):
        for key,value in [('rva',False),('raw_hex','cc'),('length',True),('assembly','int3')]:
            with self.subTest(key=key):
                r=rows();r[0][key]=value
                with self.assertRaises(d.Refused): d.fixture_listing(r)
        with self.assertRaises(d.Refused): d.fixture_listing(rows()[:-1])

    def test_complete_loader_lifecycle(self):
        x=d.loader_events(debug_text(),42)
        self.assertEqual(x['initialized'],LIBS)
        self.assertEqual(x['search_records'],3)
        self.assertFalse(x['production_environment'] or x['proves_all_mappings'])

    def test_search_attempt_is_not_a_loaded_object(self):
        text=b'42:\ttrying file=/lib/absent.so\n'+debug_text()
        self.assertNotIn('/lib/absent.so',d.loader_events(text,42)['initialized'])

    def test_wrong_process_and_noninteger_pid(self):
        for pid in (True,0,43,2**31):
            with self.assertRaises(d.Refused):d.loader_events(debug_text(),pid)
        with self.assertRaises(d.Refused):d.loader_events(debug_text()+b'43: \n',42)

    def test_unterminated_oversized_or_binary_log(self):
        for raw in (b'',debug_text()[:-1],b'x'*d.CAPTURE_CAP,b'42:\xff\n'):
            with self.assertRaises(d.Refused):d.loader_events(raw,42)

    def test_no_entry_or_fini_is_not_complete(self):
        for token in (b'initialize program',b'transferring control',b'calling fini',b'calling init'):
            text=b'\n'.join(l for l in debug_text().splitlines() if token not in l)+b'\n'
            with self.assertRaises(d.Refused):d.loader_events(text,42)

    def test_duplicate_object_and_namespace_rejected(self):
        additions=[b'42: calling init: '+LIBS[0].encode()+b'\n',b'42: calling fini:  [1]\n']
        for extra in additions:
            with self.assertRaises(d.Refused):d.loader_events(debug_text()+extra,42)

    def test_unknown_error_control_or_path_is_not_ignored(self):
        for extra in (b'42: error loading library\n',b'42: \x1b[32m\n',
                      b'42: calling init: /tmp/plugin.so\n',b'42: calling init: /lib/../tmp/p.so\n'):
            with self.assertRaises(d.Refused):d.loader_events(debug_text()+extra,42)

    def test_late_library_loading_refuses_this_initialization_contract(self):
        text=debug_text().replace(b'42:\tcalling fini:  [0]',b'42:\tcalling init: /lib/new.so\n42:\tcalling fini:  [0]')
        with self.assertRaises(d.Refused):d.loader_events(text,42)

    def test_lifecycle_order_and_wrong_executable(self):
        for text in (b'\n'.join(reversed(debug_text().splitlines()))+b'\n',
                     debug_text().replace(d.DECODER.encode(),b'/usr/bin/other')):
            with self.assertRaises(d.Refused):d.loader_events(text,42)

    def test_graph_join_uses_frozen_aliases_not_current_host(self):
        with patch.object(d.os.path,'realpath',side_effect=AssertionError('host lookup')):
            found=d.join_libraries(d.loader_events(debug_text(),42),graph())
        self.assertEqual({v['path'] for v in found},{'/usr/lib/ld.so','/usr/lib/libc.so'})

    def test_missing_extra_or_aliased_duplicate_provider_refused(self):
        for mode in ('missing','unknown','duplicate'):
            x=d.loader_events(debug_text(),42)
            if mode=='missing':x['initialized'].pop()
            elif mode=='unknown':x['initialized'].append('/lib/unregistered.so')
            else:x['initialized'].append(LIBS[0])
            with self.assertRaises(d.Refused):d.join_libraries(x,graph())

    def test_python_only_dependencies_not_required_for_decoder(self):
        g=graph();g['nodes'].append(dict(path='/usr/lib/python-only.so',bytes=1,sha256='b'*64))
        g['edges'].append(dict(source='/usr/bin/python3',provider='/usr/lib/python-only.so'))
        self.assertEqual(len(d.join_libraries(d.loader_events(debug_text(),42),g)),2)

    def test_diagnostic_only_adds_one_environment_key(self):
        from test_bcrypt_owner_backend_017 import Directory,Process
        directory=Directory('/synthetic/new')
        backend=d.PreparedNativeBackend('/synthetic/new',d.DECODER,d.FIXTURE_SHA,'d'*64,
            output_factory=lambda _:directory,encode_json=d.encode_json,parse_listing=d.parse_listing)
        backend.reserve();backend.write('target.bin',d.FIXTURE)
        vector=[d.DECODER,'synthetic argv value']
        with patch.object(d,'_single_native_thread'),patch.object(d.subprocess,'Popen',return_value=Process()) as popen:
            child=d.diagnostic_launch(backend,vector)
            with self.assertRaises(d.Refused):d.diagnostic_launch(backend,vector)
        self.assertEqual(popen.call_count,1)
        self.assertEqual(popen.call_args.args,(vector,))
        k=popen.call_args.kwargs
        self.assertEqual(k['env'],dict(d.ENV,LD_DEBUG='libs'))
        self.assertNotIn('LD_DEBUG',d.ENV)
        self.assertIs(k['preexec_fn'],d.child_limits)
        self.assertIs(k['shell'],False)
        self.assertIsInstance(child,d.RetainedChild)

    def test_default_script_not_a_formal_or_parameterized_target_entry(self):
        tree=ast.parse(Path(d.__file__).read_text())
        source=Path(d.__file__).read_text()
        self.assertNotIn('eval(',source);self.assertNotIn('os.remove',source)
        self.assertNotIn('sys.modules.pop',source)
        self.assertIn("if len(sys.argv)!=2:",source)
        self.assertEqual([n.targets[0].id for n in tree.body if isinstance(n,ast.Assign) and
                          isinstance(n.targets[0],ast.Name) and n.targets[0].id=='FIXTURE'],['FIXTURE'])


    def lifecycle(self, fault=None):
        scope=types.SimpleNamespace(active=False)
        events=[]; candidates=[]
        def activate():scope.active=True;events.append('activate')
        def drain():
            scope.active=False;events.append('drain')
            return dict(status='refused' if fault=='scope' else 'descendant_scope_complete_unreviewed')
        scope.activate=activate;scope.drain=drain;scope.assert_empty=lambda:None
        child=types.SimpleNamespace(reaped=False,process=types.SimpleNamespace(pid=42,args=[d.DECODER]))
        waits=[]
        def wait(timeout):
            waits.append(timeout)
            if fault=='timeout' and len(waits)==1:raise d.Refused('DEADLINE')
            child.reaped=True
            return -9 if fault=='timeout' else 0
        child.wait=wait;child.poll=lambda:None
        child.kill=lambda:events.append('kill')
        child.capture=lambda cap:(b'synthetic',b'')
        child.close=lambda:events.append('child-close')
        backend=types.SimpleNamespace(directory=None)
        def reserve():backend.directory=object()
        def launch(raw):
            events.append('launch')
            if fault=='launch':raise OSError('test launch error')
            return child
        def close():
            events.append('backend-close')
            if fault=='close':raise OSError('test close error')
        backend.reserve=reserve;backend.launch=launch;backend.close=close
        backend.write=lambda name,raw:events.append(name)
        backend.parse=lambda *_:rows();backend.candidate=lambda r:candidates.append(copy.deepcopy(r))
        with tempfile.TemporaryDirectory() as td,patch.object(d,'DescendantScope',return_value=scope), \
             patch.object(d,'PreparedNativeBackend',return_value=backend):
            root=Path(td)
            if fault:
                with self.assertRaises(d.Refused):d.run_case(root,'baseline')
            else:d.run_case(root,'baseline')
            report=d.json.loads((root/'baseline-result.json').read_bytes())
        return report,events,waits,candidates

    def test_case_saves_post_close_result_separately(self):
        r,events,waits,candidates=self.lifecycle()
        self.assertTrue(r['backend_closed'])
        self.assertFalse(candidates[0]['backend_closed'])
        self.assertEqual(events.count('launch'),1)
        self.assertEqual(events.count('drain'),1)

    def test_case_launch_failure_drains_and_never_retries(self):
        r,events,waits,_=self.lifecycle('launch')
        self.assertEqual(r['status'],'refused')
        self.assertEqual(events.count('drain'),1)
        self.assertEqual(waits,[])
        self.assertEqual(events.count('launch'),1)

    def test_case_timeout_reaps_and_retains_refusal(self):
        r,events,waits,_=self.lifecycle('timeout')
        self.assertEqual(waits,[3,1])
        self.assertEqual(events.count('kill'),1)
        self.assertTrue(r['reaped'] and r['capture_stable'])
        self.assertEqual(r['status'],'refused')

    def test_case_failed_scope_cannot_be_accepted(self):
        r,_,_,_=self.lifecycle('scope')
        self.assertTrue(r['cleanup_failed'])
        self.assertEqual(r['status'],'refused')

    def test_case_close_failure_does_not_rewrite_candidate(self):
        r,events,_,candidates=self.lifecycle('close')
        self.assertEqual(candidates[0]['status'],'decoder_case_complete_unreviewed')
        self.assertEqual(r['status'],'refused')
        self.assertEqual(events.count('backend-close'),1)


if __name__=='__main__': unittest.main(verbosity=2)
