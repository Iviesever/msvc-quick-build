"""Pure/runtime-file contracts only. Never invokes the live evidence collector."""
import ast
import os
from pathlib import Path
import tempfile
from types import ModuleType
from importlib.machinery import ModuleSpec
import unittest

import check_bcrypt_runtime_evidence_017 as q


def line(path=b'/usr/lib/libfake.so', inode=b'10', permissions=b'r-xp'):
    return b'1000-2000 '+permissions+b' 00001000 00:01 '+inode+b' '+path+b'\n'


def module(name='synthetic'):
    value = ModuleType(name)
    value.__spec__ = ModuleSpec(name, None, origin='/usr/lib/synthetic.py')
    value.__file__ = '/usr/lib/synthetic.py'
    value.__package__ = ''
    return value


class RuntimeEvidenceTests(unittest.TestCase):
    def test_map_projection_has_no_virtual_addresses(self):
        value=q.parse_maps(line())
        self.assertEqual(value['files']['/usr/lib/libfake.so'],dict(
            device_major=0,device_minor=1,inode=10,permissions=['r-xp']))
        self.assertNotIn('1000',repr(value))
        self.assertEqual(value['rows'],1)

    def test_duplicate_segments_join_exact_identity(self):
        value=q.parse_maps(line()+line(permissions=b'r--p'))
        self.assertEqual(value['files']['/usr/lib/libfake.so']['permissions'],['r--p','r-xp'])
        with self.assertRaises(q.EvidenceError): q.parse_maps(line()+line(inode=b'11'))

    def test_anonymous_regions_not_read_as_paths(self):
        value=q.parse_maps(line(path=b'[heap]',inode=b'0')+b'3000-4000 rw-p 0 00:00 0\n')
        self.assertEqual(value['files'],{});self.assertEqual(value['anonymous_regions'],2)
        with self.assertRaises(q.EvidenceError): q.parse_maps(line(path=b'[heap]'))

    def test_deleted_or_escaped_mapping_is_not_silently_normalized(self):
        for path in (b'/usr/lib/libfake.so (deleted)',b'/usr/lib/lib\\012.so',b'relative',b'/usr/lib/\xff'):
            with self.subTest(path=path), self.assertRaises(q.EvidenceError):q.parse_maps(line(path))

    def test_mapping_permission_validation(self):
        for permissions in (b'xxxx',b'r-x',b'rwxq',b'rwxpp'):
            with self.subTest(permissions=permissions),self.assertRaises(q.EvidenceError):
                q.parse_maps(line(permissions=permissions))

    def test_mapping_numbers_are_not_partial_parses(self):
        for raw in (line().replace(b'1000-2000',b'3000-2000'),line().replace(b'00:01',b'GG:01'),
                    line().replace(b'00001000',b'nothex'),line(inode=b'true'),line(inode=b'0')):
            with self.assertRaises(q.EvidenceError):q.parse_maps(raw)

    def test_mapping_byte_and_row_caps(self):
        for raw in (b'',bytearray(line()),b'x'*(q.MAP_CAP+1),line()*513):
            with self.assertRaises(q.EvidenceError):q.parse_maps(raw)
        self.assertEqual(q.parse_maps(line()*512)['rows'],512)

    def test_file_count_cap_before_unbounded_accumulation(self):
        raw=b''.join(line(b'/usr/lib/f'+str(i).encode()) for i in range(257))
        with self.assertRaisesRegex(q.EvidenceError,'MAP_FILES'):q.parse_maps(raw)

    def test_read_scope_rejects_secrets_and_path_traversal(self):
        for path in ('/etc/shadow','/home/runner/.ssh/key','/usr/lib/../../etc/passwd','//usr/lib/f',
                     '/repo/other/f','/repo/src2/f',True):
            with self.subTest(path=path),self.assertRaises(q.EvidenceError):
                q.admitted_path(path,Path('/repo/src'))
        for path in ('/repo/src/a.py','/usr/lib/a','/lib/a','/usr/bin/python3','/etc/ld.so.cache'):
            self.assertEqual(q.admitted_path(path,Path('/repo/src')),path)

    def test_mapping_inode_device_bind_to_file(self):
        mapping=q.parse_maps(line())['files']['/usr/lib/libfake.so']
        q.check_mapped_file(mapping,dict(state=dict(ino=10,dev=os.makedev(0,1))))
        for state in (dict(ino=11,dev=os.makedev(0,1)),dict(ino=10,dev=os.makedev(0,2))):
            with self.assertRaises(q.EvidenceError):q.check_mapped_file(mapping,dict(state=state))

    def test_observed_module_metadata_is_a_record_not_allowlist(self):
        value=module();before=q.observed_modules({'synthetic':value})
        self.assertEqual(before[0]['synthetic']['file'],'/usr/lib/synthetic.py')
        self.assertEqual(before[2],[])
        q.stable_modules(before,q.observed_modules({'synthetic':value}))
        with self.assertRaises(q.EvidenceError):q.stable_modules(before,q.observed_modules({'synthetic':module()}))

    def test_unsupported_cached_objects_are_retained_as_blockers(self):
        unknown=object();rows,refs,blockers=q.observed_modules({'unsupported':unknown})
        self.assertEqual(rows,{})
        self.assertEqual(blockers,[dict(module='unsupported',reason='MODULE_OBJECT')])
        self.assertIs(refs['unsupported'][0],unknown)

    def test_module_drift_rejected(self):
        value=module();before=q.observed_modules({'synthetic':value})
        value.__file__='/usr/lib/changed.py'
        with self.assertRaises(q.EvidenceError):q.stable_modules(before,q.observed_modules({'synthetic':value}))
        with self.assertRaises(q.EvidenceError):q.stable_modules(before,q.observed_modules({'different':module()}))

    def test_module_count_cap_and_no_lazy_attribute_invocation(self):
        with self.assertRaises(q.EvidenceError):q.observed_modules({})
        with self.assertRaises(q.EvidenceError):q.observed_modules({str(i):object() for i in range(257)})
        value=module()
        def forbidden(name):raise AssertionError('lazy module attribute access')
        value.__getattr__=forbidden
        self.assertEqual(q.observed_modules({'synthetic':value})[2],[])

    def test_output_is_exclusive_canonical_and_bounded(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'evidence.json';q.save_new(path,dict(z=1,a=False))
            self.assertEqual(path.read_bytes(),b'{"a":false,"z":1}\n')
            with self.assertRaises(FileExistsError):q.save_new(path,{})
            self.assertEqual(path.read_bytes(),b'{"a":false,"z":1}\n')
            with self.assertRaises(ImportError):q.save_new(Path(d)/'oversize',dict(s='x'*q.OUTPUT_CAP))
            self.assertFalse((Path(d)/'oversize').exists())

    def test_live_run_rejects_unknown_role_before_io(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'never'
            with self.assertRaises(q.EvidenceError):q.run('unregistered',out)
            self.assertFalse(out.exists())

    def test_fixed_preload_targets_do_not_include_tests_or_observers(self):
        self.assertEqual(q.PRELOAD,{
            'owner':('bcrypt_fixed_bootstrap_017','bcrypt_disassembly_io_014','bcrypt_disassembly_listing_014'),
            'controller':('bcrypt_observation_controller_017',)})

    def test_no_workload_or_authorization_generator(self):
        text=Path(q.__file__).read_text();tree=ast.parse(text)
        forbidden={'Popen','fork','execv','execve','system','ImportBoundary','activate'}
        calls=[n for n in ast.walk(tree) if isinstance(n,ast.Call)]
        names={n.func.attr if isinstance(n.func,ast.Attribute) else n.func.id
               for n in calls if isinstance(n.func,(ast.Attribute,ast.Name))}
        self.assertFalse(names & forbidden)
        self.assertIn('generated_allowlist=False',text)
        self.assertIn('decoder_runtime_observed=False',text)
        self.assertIn('writer_domain_qualified=False',text)
        self.assertIn('execution_admitted=False',text)


if __name__=='__main__':unittest.main(verbosity=2)
