"""Regression for the consumed V9 measurement: metadata and synthetic files only."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch
from zipfile import ZipFile
import v9_noop_validation as v
from test_v9_noop_validation import journal, save

ROOT=Path(__file__).resolve().parents[2]

class CacheWiring(unittest.TestCase):
    def test_cli_override_not_locator_fallback(self):
        # This independent producer contract and recorded path do not derive from v.CACHE.
        app=(ROOT/'cpp/src/app/Application.cpp').read_text(encoding='utf-8')
        block='    std::string toolchain_cache_name = "vs-";\n    toolchain_cache_name += mqb::to_string(options.build.architecture);\n    toolchain_cache_name += ".cache";\n    toolchain_discovery.cache_file = layout->artifact_root()\n        / "cache"\n        / "toolchain"\n        / toolchain_cache_name;'
        self.assertIn(block,app)
        self.assertLess(app.index(block),app.index('locator.discover(toolchain_discovery)'))
        self.assertEqual('.mqb/cache/toolchain/vs-x64.cache',v.CACHE)
        ps=(ROOT/'tests/native/run_v9_noop_validation.ps1').read_text()
        self.assertIn("$path=Join-Path $Fixture '.mqb/cache/toolchain/vs-x64.cache'",ps)
        self.assertNotIn('msvc-auto-x64-x64.mqbcache',ps)

    def test_recorded_prime_path_regression_without_executing_a_program(self):
        # Original public artifact10851277845: metadata only, no cache bodies/EXEs.
        path=ROOT/'tests/native/v9_noop_validation_failed_measurement.zip'
        self.assertEqual(5303,path.stat().st_size)
        self.assertEqual('c0ac5b61a9e3e0ca7956794cf221a1dd4c2bb13a10ceeb916f58f01f14a9c30b',v.file_sha(path))
        with ZipFile(path) as z:
            v.safe_members(z);self.assertIsNone(z.testzip())
            self.assertEqual({'calls/01.'+k+'.json' for k in ('before','started','result','after')} |
                {'completion.json','environment-before.json','plan.json','request.json'},set(z.namelist()))
            data={k:json.loads(z.read('calls/01.'+k+'.json')) for k in ('before','result','after')}
            plan=json.loads(z.read('plan.json'));done=json.loads(z.read('completion.json'))
        args=(v.rows()[0],data['result'],data['before'],data['after'],plan['binaries'])
        result=v.validate_call(*args)
        self.assertEqual('prime',result['row']['phase'])
        self.assertEqual('stopped',done['status']);self.assertEqual(1,done['attempted'])
        with patch.object(v,'CACHE','.mqb/cache/toolchain/msvc-auto-x64-x64.mqbcache'):
            with self.assertRaisesRegex(ValueError,'Missing output or V9 cache'):v.validate_call(*args)
        # Parsing that one valid prime does not manufacture the other15 calls.
        with journal() as (root,inputs):
            save(root/'completion.json',done)
            with patch.object(v.gate,'evaluate_pairs') as evaluate:
                with self.assertRaisesRegex(ValueError,'Stopped/incomplete run'):v.audit(root,inputs)
                evaluate.assert_not_called()

    def test_locator_fallback_file_cannot_substitute_for_cli_cache(self):
        with journal() as (root,inputs):
            for name in ('01.after','02.before','02.after'):
                path=root/'calls'/(name+'.json');data=v.load(path)
                for file in data:
                    if file['path']=='.mqb/cache/toolchain/vs-x64.cache':
                        file['path']='.mqb/cache/toolchain/msvc-auto-x64-x64.mqbcache'
                save(path,data)
            with self.assertRaisesRegex(ValueError,'Missing output or V9 cache'):v.audit(root,inputs)

    def test_control_loop_does_not_mock_the_cache_projection(self):
        control=(ROOT/'tests/native/test_v9_noop_validation_control.ps1').read_text()
        self.assertIn("'Get-V9CacheProjection'",control)
        self.assertNotRegex(control,r'function\s+Get-V9CacheProjection\s*\(')
        self.assertIn('Write-SyntheticCache',control)

if __name__=='__main__':unittest.main(verbosity=2)
