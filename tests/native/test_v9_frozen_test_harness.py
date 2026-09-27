"""The test fixture preserves old admission, not permission to rerun a study."""
from pathlib import Path
import hashlib
import tempfile
import unittest
from zipfile import ZipFile

import v9_frozen_test_harness as h
import v9_noop_validation as v
import v9_noop_rebound as b
import v9_noop_samejob as s

ROOT = Path(__file__).resolve().parents[2]

class FrozenTestData(unittest.TestCase):
    def source_copy(self, destination):
        # Minimum input inventory, not a fake full product checkout.
        for name in h.PINS:
            path = h.FIXTURE if name == h.LAYOUT else name
            target = destination / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((ROOT/path).read_bytes())

    def test_all_original_verifiers_accept_the_pinned_test_data(self):
        with h.pinned_test_harness(ROOT) as replay:
            v.check_repo(replay)
            b.check_repo(replay)
            s.check_repo(replay)
            self.assertEqual(17, len([p for p in replay.rglob('*') if p.is_file()]))

    def test_moving_inventory_is_still_refused_not_relabelled(self):
        self.assertNotEqual(h.PINS[h.LAYOUT],
                            hashlib.sha256(h.canonical(h.LAYOUT,(ROOT/h.LAYOUT).read_bytes())).hexdigest())
        for check in (v.check_repo,b.check_repo,s.check_repo):
            with self.subTest(check=check.__module__), self.assertRaisesRegex(ValueError,'assert_cpp_layout'):
                check(ROOT)

    def test_changed_fixture_is_refused_before_destination_created(self):
        with tempfile.TemporaryDirectory() as temp:
            current=Path(temp)/'source';self.source_copy(current)
            (current/h.FIXTURE).write_bytes(b'changed inventory')
            output=Path(temp)/'snapshot'
            with self.assertRaisesRegex(ValueError,'Frozen test member changed'):
                h.build_snapshot(current,output)
            self.assertFalse(output.exists())

    def test_missing_helper_is_not_repaired_or_downloaded(self):
        with tempfile.TemporaryDirectory() as temp:
            current=Path(temp)/'source';self.source_copy(current)
            (current/'tests/native/v9_noop_samejob.py').unlink()
            output=Path(temp)/'snapshot'
            with self.assertRaisesRegex(ValueError,'Missing/unsafe'):
                h.build_snapshot(current,output)
            self.assertFalse(output.exists())

    def test_changed_snapshot_fails_original_gate(self):
        with h.pinned_test_harness(ROOT) as replay:
            (replay/h.LAYOUT).write_bytes(b'changed after snapshot')
            with self.assertRaisesRegex(ValueError,'assert_cpp_layout'):
                s.check_repo(replay)

    def test_fresh_snapshot_and_archive_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            replay=Path(temp)/'snapshot'
            h.build_snapshot(ROOT,replay)
            with self.assertRaises(FileExistsError):
                h.build_snapshot(ROOT,replay)
            archive=Path(temp)/'synthetic.zip'
            h.synthetic_archive(replay,archive)
            with self.assertRaises(FileExistsError):
                h.synthetic_archive(replay,archive)

    def test_synthetic_archive_is_not_claimed_as_git_checkout(self):
        with tempfile.TemporaryDirectory() as temp:
            replay=Path(temp)/'snapshot'
            meta=h.build_snapshot(ROOT,replay)
            self.assertFalse(meta['full_product_checkout'])
            self.assertFalse(meta['execution_authorized'])
            archive=Path(temp)/'synthetic.zip';h.synthetic_archive(replay,archive)
            with ZipFile(archive) as z:
                self.assertEqual(b'c'*40,z.comment)
                self.assertNotEqual(h.ORIGIN.encode(),z.comment)
                self.assertEqual(set(h.PINS),set(z.namelist()))
                for name,digest in h.PINS.items():
                    self.assertEqual(digest,hashlib.sha256(z.read(name)).hexdigest())
            identity,_=s.source_archive(archive,h.SYNTHETIC_COMMIT)
            self.assertEqual(17,identity['files'])

    def test_current_sources_untouched_and_context_removed(self):
        before={name: (ROOT/name).read_bytes() for name in h.PINS}
        with h.pinned_test_harness(ROOT) as replay:
            retained=replay
        self.assertFalse(retained.exists())
        self.assertEqual(before,{name: (ROOT/name).read_bytes() for name in h.PINS})

if __name__=='__main__':
    unittest.main(verbosity=2)
