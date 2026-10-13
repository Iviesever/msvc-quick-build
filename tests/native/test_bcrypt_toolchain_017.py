"""Temporary synthetic ELF/filesystem controls, without native processes."""
from pathlib import Path
import hashlib
import os
import tempfile
import unittest
from unittest import mock

import bcrypt_toolchain_017 as t
from test_bcrypt_elf_inventory_017 import specimen


class InventoryTests(unittest.TestCase):
    def test_snapshot_digest(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'file'; p.write_bytes(b'host')
            r,data=t.snapshot(str(p), t.Budget(), keep=True)
            self.assertEqual(data,b'host')
            self.assertEqual(r['sha256'],hashlib.sha256(b'host').hexdigest())

    def test_snapshot_budget_before_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'file'; p.write_bytes(b'host')
            with mock.patch.object(t.os,'read') as read, self.assertRaises(t.Refused):
                t.snapshot(str(p),t.Budget(limit=3))
            read.assert_not_called()

    def test_snapshot_short_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'file'; p.write_bytes(b'host')
            with mock.patch.object(t.os,'read',return_value=b''),self.assertRaises(t.Refused):
                t.snapshot(str(p),t.Budget())

    def test_snapshot_growth(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'file'; p.write_bytes(b'x')
            with mock.patch.object(t.os,'read',side_effect=[b'x',b'z']),self.assertRaises(t.Refused):
                t.snapshot(str(p),t.Budget())

    def test_fifo_no_wait(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'fifo'; os.mkfifo(p)
            with self.assertRaises(t.Refused): t.snapshot(str(p),t.Budget())

    def test_directory_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(t.Refused): t.snapshot(tmp,t.Budget())

    def test_symlink_resolved_and_recorded(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'data';p.write_bytes(b'x');q=Path(tmp)/'link';q.symlink_to(p)
            r,_=t.snapshot(str(q),t.Budget())
            self.assertEqual(r['path'],str(p))

    def test_deadline(self):
        with self.assertRaises(t.Refused): t.Budget(seconds=-1).checkpoint()

    def test_search_origin(self):
        md=dict(runpath='$ORIGIN/../lib',rpath=None)
        self.assertEqual(t.search_paths(md,'/a/bin/python',('/usr/lib',)),('/a/lib','/usr/lib'))

    def test_search_runpath_precedence_record_only(self):
        md=dict(runpath='/a',rpath='/b')
        self.assertEqual(t.search_paths(md,'/bin/x',('/usr/lib',)),('/a','/usr/lib'))

    def test_unsafe_search_values(self):
        for value in ('',':/a','/a:', 'relative', '$LIB', '$PLATFORM', ':'.join('/x' for _ in range(17))):
            with self.subTest(value=value),self.assertRaises(t.Refused):
                t.search_paths(dict(runpath=value,rpath=None),'/x/tool',('/usr/lib',))

    def test_dependency_absent(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(t.Refused): t.resolve_needed('libmissing.so',(tmp,))

    def test_dependency_ambiguity_not_first_wins(self):
        with tempfile.TemporaryDirectory() as tmp:
            a,b=Path(tmp)/'a',Path(tmp)/'b';a.mkdir();b.mkdir()
            (a/'libx.so').write_bytes(b'x');(b/'libx.so').write_bytes(b'x')
            with self.assertRaises(t.Refused):t.resolve_needed('libx.so',(str(a),str(b)))

    def test_aliases_of_same_library_are_not_ambiguous(self):
        with tempfile.TemporaryDirectory() as tmp:
            a,b=Path(tmp)/'a',Path(tmp)/'b';a.mkdir();b.symlink_to(a,target_is_directory=True)
            (a/'libx.so').write_bytes(b'x')
            p,aliases=t.resolve_needed('libx.so',(str(a),str(b)))
            self.assertEqual(p,str(a/'libx.so'));self.assertEqual(len(aliases),2)

    def test_slash_dependency_refused(self):
        with self.assertRaises(t.Refused):t.resolve_needed('../file',('/tmp',))

    def make_host(self, root):
        lib=root/'lib';lib.mkdir()
        (lib/'libshared.so').write_bytes(specimen(needed=(),interp=None))
        for name in ('python','decoder'):
            (root/name).write_bytes(specimen(needed=('libshared.so',),interp=None))
        (root/'cache').write_bytes(b'fake loader cache, not executed')
        return dict(python_path=str(root/'python'),decoder_path=str(root/'decoder'),
                    defaults=(str(lib),),cache_path=str(root/'cache'),preload_path=str(root/'preload'))

    def test_complete_fake_graph_stays_unadmitted(self):
        with tempfile.TemporaryDirectory() as tmp:
            r=t.collect(**self.make_host(Path(tmp)))
            self.assertEqual((r['node_count'],r['edge_count']),(3,2))
            self.assertEqual(r['reads']['complete_passes'],8)
            self.assertTrue(r['hooks_absent'])
            self.assertIs(r['execution_admitted'],False)
            self.assertIs(r['runtime_loader_verified'],False)
            self.assertEqual(r['decoder_processes'],0)

    def test_existing_hook_not_hidden(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);args=self.make_host(root);(root/'preload').write_bytes(b'fake')
            r=t.collect(**args);self.assertFalse(r['hooks_absent']);self.assertIs(r['execution_admitted'],False)

    def test_transitive_edge(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);args=self.make_host(root)
            (root/'lib/libshared.so').write_bytes(specimen(needed=('libleaf.so',),interp=None))
            (root/'lib/libleaf.so').write_bytes(specimen(needed=(),interp=None))
            r=t.collect(**args);self.assertEqual(r['node_count'],4);self.assertEqual(r['edge_count'],3)

    def test_dependency_cycle_is_finite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);args=self.make_host(root)
            (root/'lib/libshared.so').write_bytes(specimen(needed=('libshared.so',),interp=None))
            r=t.collect(**args);self.assertEqual(r['node_count'],3);self.assertEqual(r['edge_count'],3)

    def test_missing_transitive_dependency_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);args=self.make_host(root)
            (root/'lib/libshared.so').write_bytes(specimen(needed=('libleaf.so',),interp=None))
            with self.assertRaises(t.Refused): t.collect(**args)

    def test_node_cap(self):
        with tempfile.TemporaryDirectory() as tmp:
            args=self.make_host(Path(tmp))
            with mock.patch.object(t,'NODE_CAP',2),self.assertRaises(t.Refused):t.collect(**args)

    def test_second_pass_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            args=self.make_host(Path(tmp));original=t.snapshot;calls=0
            def changed(*a,**k):
                nonlocal calls
                calls+=1;record,data=original(*a,**k)
                if calls==5:record['sha256']='0'*64
                return record,data
            with mock.patch.object(t,'snapshot',side_effect=changed),self.assertRaises(t.Refused):t.collect(**args)

    def test_candidate_directory_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp)/'libx.so').mkdir()
            with self.assertRaises(t.Refused):t.resolve_needed('libx.so',(tmp,))


if __name__=='__main__':unittest.main(verbosity=2)
