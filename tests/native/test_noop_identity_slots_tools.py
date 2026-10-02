"""Prerequisite discovery regression; do not dispatch the consumed slot-001 study."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import noop_identity_slots_workflow_contract as pins

ROOT = Path(__file__).resolve().parents[2]


class PrerequisiteDiscoveryTests(unittest.TestCase):
    def test_entry_uses_scalar_resolver_for_both_prerequisites(self):
        entry = (ROOT/'tests/native/run_noop_identity_slots.ps1').read_text(encoding='utf-8')
        for name in ('git', 'python'):
            self.assertEqual(1, entry.count(f"${name}=Resolve-SlotApplication '{name}'"))
            self.assertNotIn(f'${name}=(Get-Command', entry)
        self.assertLess(entry.index('Assert-SlotAdmission'), entry.index("$git=Resolve-SlotApplication"))
        self.assertLess(entry.index("$python=Resolve-SlotApplication"), entry.index('& $git -C $repo rev-parse HEAD'))

    def test_exact_entry_pins_and_consumed_workflow_remain_bound(self):
        for name, expected in pins.ENTRY_PINS.items():
            content = (ROOT/name).read_text(encoding='utf-8').encode()
            self.assertEqual(expected, hashlib.sha256(content).hexdigest(), name)
        self.assertEqual('73425e14a4e523deaef53d47aa2fcebf842ddb606cfe7f4eb210413c6c388e0f',
                         hashlib.sha256((ROOT/pins.WORKFLOW).read_text(encoding='utf-8').encode()).hexdigest())

    def test_real_powershell_multiple_path_matches_and_selected_children(self):
        pwsh = shutil.which('pwsh')
        if pwsh is None:
            self.skipTest('PowerShell unavailable; real prerequisite resolution not executed')
        retained = ROOT/'journal-checks'
        with tempfile.TemporaryDirectory() as temp:
            out = (retained/'slot-tool-controls') if os.environ.get('GITHUB_ACTIONS') == 'true' and (retained/'identity.json').is_file() else Path(temp)/'controls'
            out.mkdir(exist_ok=False)
            for folder in ('first tools', 'second tools', 'empty'):
                directory = out/folder
                directory.mkdir()
                if folder == 'empty':
                    continue
                label, code = ('FIRST', 0) if folder == 'first tools' else ('SECOND', 37)
                for name in ('git', 'python'):
                    path = directory/(name+'.cmd' if os.name == 'nt' else name)
                    data = f'@echo off\r\necho {label}-{name}\r\nexit /b {code}\r\n' if os.name == 'nt' else f'#!/bin/sh\nprintf "%s\\n" "{label}-{name}"\nexit {code}\n'
                    path.write_bytes(data.encode('ascii'))
                    if os.name != 'nt':
                        path.chmod(0o755)
            args = [pwsh, '-NoLogo', '-NoProfile', '-NonInteractive', '-File',
                    str(ROOT/'tests/native/test_noop_identity_slots_tools.ps1'),
                    '-FixtureRoot', str(out.resolve()), '-OutputPath', str((out/'result.json').resolve())]
            with (out/'stdout.txt').open('xb') as stdout, (out/'stderr.txt').open('xb') as stderr:
                run = subprocess.run(args, stdout=stdout, stderr=stderr, timeout=35, check=False)
            logs = (out/'stdout.txt').read_text(errors='replace') + (out/'stderr.txt').read_text(errors='replace')
            self.assertEqual(0, run.returncode, logs)
            result = json.loads((out/'result.json').read_text(encoding='utf-8-sig'))
            self.assertEqual(10, result['tests'])
            self.assertEqual(0, result['failures'], result)
            self.assertTrue(all(case['passed'] for case in result['cases']), result)
            self.assertEqual(4, result['harmless_child_launches'])
            for field in ('study_mqb_calls', 'msvc_calls', 'workflow_dispatches'):
                self.assertEqual(0, result[field])


if __name__ == '__main__':
    unittest.main(verbosity=2)
