"""Actual Git -> Python -> pinned ZIP -> prepare -> preflight, with NO MQB launch."""
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
from zipfile import ZipFile

import noop_identity_slots as slots
import noop_identity_slots_recovery as recovery
from test_noop_identity_slots_recovery import failed_input

ROOT=Path(__file__).resolve().parents[2]


class PreparationChainTests(unittest.TestCase):
    def test_shared_preparation_boundary_has_no_measurement_edge(self):
        text=(ROOT/'tests/native/noop_identity_slots_runtime.psm1').read_text()
        body=text.split('function Invoke-SlotPreparation {',1)[1].split('function Write-NewJson {',1)[0]
        for token in ('Invoke-SlotStudy','Invoke-SlotBlock','New-SlotState','Capture','Launcher'):
            self.assertNotIn(token,body)
        self.assertLess(body.index("Resolve-SlotApplication 'git'"),body.index('& $git -C $RepoRoot rev-parse HEAD'))
        self.assertLess(body.index('& $git -C $RepoRoot status'),body.index('& $python -B $tool prepare'))
        self.assertLess(body.index('& $python -B $tool prepare'),body.index('& $python -B $tool native-preflight'))
        entry=(ROOT/'tests/native/run_noop_identity_slots.ps1').read_text()
        self.assertLess(entry.index('Assert-SlotAdmission'),entry.index('$prepared=Invoke-SlotPreparation'))
        self.assertLess(entry.index('$prepared=Invoke-SlotPreparation'),entry.index('Invoke-SlotStudy $state'))
        self.assertNotIn('Invoke-SlotStudy',(ROOT/'tests/native/test_noop_identity_slots_preparation.ps1').read_text())

    def test_real_preparation_chain_with_multiple_tools_and_failures(self):
        pwsh=shutil.which('pwsh');git=shutil.which('git');python=shutil.which('python')
        if not pwsh:self.skipTest('PowerShell unavailable; real preparation chain not run')
        self.assertIsNotNone(git);self.assertIsNotNone(python)
        failure=failed_input(); recovery.inspect_failure(failure)
        retained=ROOT/'journal-checks'
        with tempfile.TemporaryDirectory() as temp:
            out=retained/'slot-preparation-chain' if os.environ.get('GITHUB_ACTIONS')=='true' and (retained/'identity.json').exists() else Path(temp)/'chain'
            out.mkdir(exist_ok=False);repo=out/'fixture-repo';repo.mkdir()
            for name in (*slots.SOURCE_FILES,slots.LEGACY):
                target=repo/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((ROOT/name).read_bytes())
            def git_run(*args):
                return subprocess.check_output([git,'-C',str(repo),*args],stderr=subprocess.STDOUT,text=True).strip()
            git_run('init','-q');git_run('config','user.name','MQB preparation fixture')
            git_run('config','user.email','fixture@localhost');git_run('config','core.autocrlf','false')
            git_run('add','.');git_run('-c','commit.gpgsign=false','commit','-qm','Synthetic preparation repository; no measurement')
            head=git_run('rev-parse','HEAD')
            archive=out/'original-819.zip'
            with ZipFile(failure) as z:archive.write_bytes(z.read(recovery.ORIGINAL_MEMBER))
            slots.check_archive(archive)
            folders={name:out/name for name in ('first tools','second tools','git only','python only','git failure','python failure')}
            for key,folder in folders.items():
                folder.mkdir()
                for tool,exe in [('git',git),('python',python)]:
                    if key=='git only' and tool=='python' or key=='python only' and tool=='git':continue
                    code=37 if key==tool+' failure' else None
                    suffix='.cmd' if os.name=='nt' else ''
                    path=folder/(tool+suffix)
                    if os.name=='nt':
                        content='@echo off\r\n'+(f'exit /b {code}\r\n' if code else f'"{exe}" %*\r\nexit /b %errorlevel%\r\n')
                    else:content='#!/bin/sh\n'+(f'exit {code}\n' if code else f'exec {shlex.quote(exe)} "$@"\n')
                    path.write_bytes(content.encode());path.chmod(0o755)
            results=[]
            cases=('multiple','reverse','missing_git','missing_python','git_nonzero','python_nonzero','wrong_head','dirty','bad_zip','existing')
            for case in cases:
                with self.subTest(case=case):
                    target=out/('prepared-'+case)
                    names=['first tools','second tools']
                    if case=='reverse':names.reverse()
                    if case=='missing_git':names=['python only']
                    if case=='missing_python':names=['git only']
                    if case=='git_nonzero':names=['git failure']
                    if case=='python_nonzero':names=['python failure']
                    env=dict(os.environ,PATH=os.pathsep.join(str(folders[n].resolve()) for n in names))
                    for key in ('MQB_SLOT_DISPOSABLE','MQB_SLOT_ALLOCATION','GH_TOKEN','GITHUB_TOKEN'):
                        env.pop(key,None)
                    test_archive=archive
                    if case=='bad_zip':
                        test_archive=out/'corrupt.zip';test_archive.write_bytes(b'NOT ORIGINAL')
                    if case=='existing':target.mkdir();(target/'keep').write_bytes(b'KEEP')
                    modified=repo/slots.LEGACY;original=modified.read_bytes()
                    if case=='dirty':modified.write_bytes(original+b'\n# dirty')
                    args=[pwsh,'-NoLogo','-NoProfile','-NonInteractive','-File',str(ROOT/'tests/native/test_noop_identity_slots_preparation.ps1'),
                        '-ArtifactPath',str(test_archive.resolve()),'-OutputRoot',str(target.resolve()),
                        '-RepoRoot',str(repo.resolve()),'-ReviewedCommit','f'*40 if case=='wrong_head' else head]
                    try:
                        with (out/(case+'.stdout')).open('xb') as stdout,(out/(case+'.stderr')).open('xb') as stderr:
                            completed=subprocess.run(args,stdout=stdout,stderr=stderr,env=env,timeout=20,check=False)
                    finally:
                        if case=='dirty':modified.write_bytes(original)
                    positive=case in ('multiple','reverse')
                    results.append(dict(case=case,exit_code=completed.returncode,expected_success=positive,
                        prepared=(target/'prepared.json').exists(),preflight=(target/'native-preflight.json').exists(),
                        measured_calls=0,preparation_only=True))
                    slots.b.write_new(out/(case+'.result.json'),results[-1])
                    if positive:
                        self.assertEqual(0,completed.returncode,(out/(case+'.stderr')).read_text(errors='replace'))
                        value=slots.b.load(target/'preparation-only.json')
                        self.assertEqual(0,value['mqb_calls']);self.assertFalse(value['execution_authority'])
                        self.assertEqual(str(folders[names[0]].resolve()),str(Path(value['python']).parent))
                        actual=slots.native_preflight(target.resolve(),repo.resolve(),head,'pr232-slot-001')
                        self.assertEqual(0,actual['mqb_calls'])
                        for folder in ('slots','blocks','fixtures','retired'):self.assertEqual([],list((target/folder).iterdir()))
                    else:
                        self.assertNotEqual(0,completed.returncode)
                        self.assertFalse((target/'native-preflight.json').exists())
                    self.assertFalse((target/'host.json').exists());self.assertFalse((target/'completion.json').exists())
                    if case=='existing':self.assertEqual(b'KEEP',(target/'keep').read_bytes())
            slots.b.write_new(out/'result.json',dict(cases=results,tests=len(results),status='unreviewed_case_log',
                original_input_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                original_mqb_calls=0,msvc_calls=0,workflow_dispatches=0,execution_authority=False))


if __name__=='__main__':unittest.main(verbosity=2)
