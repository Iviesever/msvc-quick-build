"""No decoder process: fake probes and local temporary ordinary files only."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import bcrypt_environment_017 as m


class EnvironmentTests(unittest.TestCase):
    def matching(self, path):
        p = next(v for v in m.OLD_PINS.values() if v["path"] == path)
        return {"status": "OBSERVED", "bytes": p["bytes"], "sha256": p["sha256"]}

    def test_matching_files_are_not_execution_admission(self):
        r = m.check_environment(self.matching)
        self.assertEqual(r["status"], "OLD_FILES_MATCH_ONLY")
        self.assertIs(r["execution_admitted"], False)
        self.assertIs(r["tool_dependencies_requalified"], False)

    def test_missing_interpreter_blocks(self):
        r = m.check_environment(lambda p: {"status": "MISSING"} if p == m.OLD_PINS["python"]["path"] else self.matching(p))
        self.assertEqual(r["status"], "BLOCKED_ENVIRONMENT_CHANGED")

    def test_replaced_decoder_same_size_blocks(self):
        def probe(path):
            result = self.matching(path)
            if path == m.OLD_PINS["decoder"]["path"]: result["sha256"] = "0"*64
            return result
        self.assertEqual(m.check_environment(probe)["status"], "BLOCKED_ENVIRONMENT_CHANGED")

    def test_size_change_blocks(self):
        def probe(path):
            result = self.matching(path); result["bytes"] += 1; return result
        self.assertEqual(m.check_environment(probe)["status"], "BLOCKED_ENVIRONMENT_CHANGED")

    def test_only_two_fixed_non_image_paths_probed(self):
        calls = []
        m.check_environment(lambda p: calls.append(p) or {"status": "MISSING"})
        self.assertEqual(calls, [v["path"] for v in m.OLD_PINS.values()])
        self.assertFalse(any("bcrypt-image" in p for p in calls))

    @unittest.skipUnless(hasattr(os, "O_NOFOLLOW"), "Linux read-only probe")
    def test_regular_file_digest(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)/"file"; p.write_bytes(b"synthetic")
            self.assertEqual(m.probe_file(str(p)), {"status":"OBSERVED", "bytes":9, "sha256":hashlib.sha256(b"synthetic").hexdigest()})

    @unittest.skipUnless(hasattr(os, "O_NOFOLLOW"), "Linux read-only probe")
    def test_missing_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(m.probe_file(str(Path(tmp)/"absent"))["status"], "MISSING")

    @unittest.skipUnless(hasattr(os, "O_NOFOLLOW"), "Linux read-only probe")
    def test_symlink_leaf_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"file"; p.write_bytes(b"x"); q=Path(tmp)/"alias"; q.symlink_to(p)
            self.assertEqual(m.probe_file(str(q))["status"], "OPEN_REFUSED")

    @unittest.skipUnless(hasattr(os, "O_NOFOLLOW"), "Linux read-only probe")
    def test_directory_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(m.probe_file(tmp)["status"], "KIND_OR_SIZE_REFUSED")

    @unittest.skipUnless(hasattr(os, "mkfifo"), "Linux FIFO control")
    def test_fifo_refused_without_waiting_for_writer(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"fifo"; os.mkfifo(p)
            self.assertEqual(m.probe_file(str(p))["status"], "KIND_OR_SIZE_REFUSED")

    @unittest.skipUnless(hasattr(os, "O_NOFOLLOW"), "Linux read-only probe")
    def test_size_cap_before_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"file"; p.write_bytes(b"1234")
            with mock.patch.object(m, "FILE_CAP", 3), mock.patch.object(m.os, "read") as read:
                self.assertEqual(m.probe_file(str(p))["status"], "KIND_OR_SIZE_REFUSED")
                read.assert_not_called()

    @unittest.skipUnless(hasattr(os, "O_NOFOLLOW"), "Linux read-only probe")
    def test_truncated_read_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"file"; p.write_bytes(b"1234")
            with mock.patch.object(m.os, "read", return_value=b""):
                self.assertEqual(m.probe_file(str(p))["status"], "SHORT_READ")

    @unittest.skipUnless(hasattr(os, "O_NOFOLLOW"), "Linux read-only probe")
    def test_growth_detector_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"file"; p.write_bytes(b"1234")
            with mock.patch.object(m.os, "read", side_effect=[b"1234", b"x"]):
                self.assertEqual(m.probe_file(str(p))["status"], "UNSTABLE_FILE")


if __name__ == "__main__":
    unittest.main()
