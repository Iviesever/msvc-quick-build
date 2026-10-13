"""017 review regressions: synthetic ELF and temporary paths only; no child process."""
from pathlib import Path
import os
import struct
import sys
import tempfile
import unittest
from unittest import mock

import bcrypt_elf_inventory_017 as elf
import bcrypt_toolchain_017 as tool
import test_bcrypt_elf_inventory_017 as elf_fixture
import test_bcrypt_toolchain_017 as host_fixture


class DependencyAdmissionTests(unittest.TestCase):
    def count_decodes(self, count):
        decoded = []
        previous = sys.getprofile()
        def observe(frame, event, arg):
            if (event == 'call' and frame.f_code.co_name == 'text' and
                    frame.f_code.co_filename == elf.parse_elf.__code__.co_filename):
                decoded.append(1)
        sys.setprofile(observe)
        try:
            data = bytes(elf_fixture.specimen(needed=('x',)*count))
            try:
                result = elf.parse_elf(data)
            except elf.Refused as exc:
                return len(decoded), str(exc), None
            return len(decoded), None, result
        finally:
            sys.setprofile(previous)

    def test_excess_dependency_count_rejected_before_any_text_decode(self):
        decodes, error, result = self.count_decodes(65)
        self.assertEqual(error, 'too many dependencies')
        self.assertIsNone(result)
        self.assertEqual(decodes, 0)

    def test_exact_dependency_limit_has_positive_decode_control(self):
        decodes, error, result = self.count_decodes(64)
        self.assertIsNone(error)
        self.assertEqual(decodes, 64)
        self.assertEqual(len(result['needed']), 64)

    def test_dynamic_file_and_memory_mapping_must_agree(self):
        data = elf_fixture.specimen()
        struct.pack_into('<Q', data, 120+16, 0x400900)
        with self.assertRaisesRegex(elf.Refused, 'dynamic segment mapping'):
            elf.parse_elf(bytes(data))

    def test_dynamic_segment_cannot_be_outside_load(self):
        data = elf_fixture.specimen()
        struct.pack_into('<Q', data, 120+16, 0x500800)
        with self.assertRaisesRegex(elf.Refused, 'dynamic segment mapping'):
            elf.parse_elf(bytes(data))

    def test_dynamic_segment_cannot_have_two_load_mappings(self):
        data = elf_fixture.specimen()
        # Second LOAD covers only the dynamic table, not its string table.
        struct.pack_into('<IIQQQQQQ', data, 232, 1, 6, 0x800, 0x400800,
                         0, 0x100, 0x100, 1)
        with self.assertRaisesRegex(elf.Refused, 'dynamic segment mapping'):
            elf.parse_elf(bytes(data))


class TokenTests(unittest.TestCase):
    def paths(self, value):
        return tool.search_paths(dict(runpath=value, rpath=None),
                                 '/safe/bin/decoder', ('/safe/lib',))

    def test_origin_is_a_token_not_an_identifier_prefix(self):
        for value in ('$ORIGINAL/lib', '$ORIGIN_suffix/lib', '$ORIGIN2/lib'):
            with self.subTest(value=value), self.assertRaises(tool.Refused):
                self.paths(value)

    def test_braced_and_plain_origin_preserved(self):
        for value in ('${ORIGIN}/../lib', '$ORIGIN/../lib'):
            self.assertEqual(self.paths(value), ('/safe/lib',))

    def test_braced_suffix_remains_supported(self):
        self.assertEqual(self.paths('${ORIGIN}_suffix/lib'),
                         ('/safe/bin_suffix/lib', '/safe/lib'))

    def test_unknown_token_stays_refused(self):
        with self.assertRaises(tool.Refused):
            self.paths('${LIB}/x')


class ResolutionRecheckTests(unittest.TestCase):
    def fixture(self, root):
        args = host_fixture.InventoryTests().make_host(root)
        other = root/'second'; other.mkdir()
        args['defaults'] = (*args['defaults'], str(other))
        return args, other/'libshared.so'

    def after_cache(self, args, change):
        original = tool.snapshot
        changed = False
        def snapshot(path, *a, **kw):
            nonlocal changed
            result = original(path, *a, **kw)
            if path == args['cache_path'] and not changed:
                changed = True
                change()
            return result
        return mock.patch.object(tool, 'snapshot', side_effect=snapshot)

    def test_new_distinct_provider_makes_second_pass_refuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            args, candidate = self.fixture(Path(tmp))
            change = lambda: candidate.write_bytes(elf_fixture.specimen(interp=None))
            with self.after_cache(args, change), self.assertRaisesRegex(
                    tool.Refused, 'ambiguous dependency'):
                tool.collect(**args)

    def test_new_same_object_alias_still_changes_recorded_candidate_set(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); args, candidate = self.fixture(root)
            change = lambda: candidate.symlink_to(root/'lib/libshared.so')
            with self.after_cache(args, change), self.assertRaisesRegex(
                    tool.Refused, 'dependency selection drift'):
                tool.collect(**args)

    def test_disappearing_unselected_hardlink_alias_is_not_silent(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); args, candidate = self.fixture(root)
            # Use directory alias removal, so the selected file's nlink/ctime stay fixed.
            candidate.symlink_to(root/'lib/libshared.so')
            with self.after_cache(args, candidate.unlink), self.assertRaises(
                    (tool.Refused, FileNotFoundError)):
                tool.collect(**args)

    def test_stable_two_directory_selection_is_still_unadmitted(self):
        with tempfile.TemporaryDirectory() as tmp:
            args, _ = self.fixture(Path(tmp))
            result = tool.collect(**args)
            self.assertEqual(result['reads']['complete_passes'], 8)
            self.assertEqual((result['node_count'], result['edge_count']), (3, 2))
            for name in ('execution_admitted', 'runtime_loader_verified',
                         'source_independently_reviewed',
                         'python_stdlib_and_dynamic_imports_qualified'):
                self.assertIs(result[name], False)

    def test_stable_same_object_aliases_are_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); args, candidate = self.fixture(root)
            candidate.symlink_to(root/'lib/libshared.so')
            result = tool.collect(**args)
            self.assertEqual(result['node_count'], 3)
            self.assertIs(result['execution_admitted'], False)

    def test_recheck_keeps_cooperative_deadline(self):
        with tempfile.TemporaryDirectory() as tmp:
            args, _ = self.fixture(Path(tmp))
            original = tool.resolve_needed
            resolutions = 0
            def resolve(*a, **kw):
                nonlocal resolutions
                resolutions += 1
                return original(*a, **kw)
            original_check = tool.Budget.checkpoint
            def checkpoint(budget):
                if resolutions >= 3:
                    raise tool.Refused('cooperative inventory deadline')
                return original_check(budget)
            with mock.patch.object(tool, 'resolve_needed', side_effect=resolve), \
                    mock.patch.object(tool.Budget, 'checkpoint', checkpoint), \
                    self.assertRaisesRegex(tool.Refused, 'deadline'):
                tool.collect(**args)


if __name__ == '__main__':
    unittest.main(verbosity=2)
