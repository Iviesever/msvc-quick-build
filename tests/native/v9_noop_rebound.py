"""Explicit old-preparation/new-execution binding. No preparation or execution here.

The consumed run stays invalid. The reserved new opportunity is NOT an allocation.
No requests/manifests/environment variables are rewritten to satisfy old admission.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from zipfile import BadZipFile, ZipFile

import v9_noop_validation as v

WORKFLOW = '.github/workflows/v9-noop-rebound.yml'
ALLOCATION = 'v9-reader-noop-002'
KEYS = tuple(k for k in v.KEYS if k not in ('PHASE','PREP_RUN','PREP_ARTIFACT','PREP_SHA256','MANIFEST_SHA256'))
IDENTITY = 'tests/native/v9_noop_rebound_preparation.json'
IDENTITY_SHA = '5d3d24888dc1d4ab54b81eba02e851d291ac15e74f330f3b2909c61eb43ac6f2'
FAILURE = 'tests/native/v9_noop_validation_failed_measurement.zip'
FAILURE_SHA = 'c0ac5b61a9e3e0ca7956794cf221a1dd4c2bb13a10ceeb916f58f01f14a9c30b'
FAILURE_RUN = '36107949309'
CORE_PINS = {
    'tests/native/v9_noop_validation.py': 'd1621c5fe9a00ea32d23a494a97b393c7b40e98594cab7a7ef3756da84c395b6',
    'tests/native/run_v9_noop_validation.ps1': '00aae69c629ae6c1c08a9bd86766b68aea2c4b7db109e69637e551a7b46afa05',
    v.WORKFLOW: '35f37126c30ce91320af2fa35c8f9046f7265551e63bde3d36188a2562feac53',
}
ROOT = Path(__file__).resolve().parents[2]
need, load, write = v.need, v.load, v.write


def frozen():
    raw = (ROOT/IDENTITY).read_bytes()
    need(v.sha(v.canonical(raw)) == IDENTITY_SHA, 'Preparation identity file changed')
    return json.loads(raw)


def protocol():
    f = frozen()
    return dict(schema=1, purpose='V9 explicit rebound after validator failure; not a retry or original #701 replay',
                preparation_harness=f['harness_commit'], prepared_run=f['run'], prepared_artifact=f['artifact'],
                prepared_sha256=f['archive_sha256'], manifest_sha256=f['members']['manifest.json'],
                consumed_run=FAILURE_RUN, consumed_artifact='10851277845', consumed_sha256=FAILURE_SHA,
                sources={s:dict(p) for s,p in v.SOURCES.items()}, argv=list(v.boundary.ARGV), rows=v.rows(),
                study_mqb_ceiling=16, previous_study_calls=121, proposed_study_total=137,
                historical_preparation_calls=5, new_preparation_calls=0,
                allocation=ALLOCATION, measurement_allocated=False, clears_hold=False, cause=None)


def admit(context):
    need(isinstance(context,dict) and set(context) == set(KEYS), 'Missing/extra execution request fields')
    fixed = dict(GITHUB_REPOSITORY=v.REPO, GITHUB_ACTIONS='true', GITHUB_EVENT_NAME='workflow_dispatch',
                 GITHUB_REF='refs/heads/main', GITHUB_WORKFLOW_REF=f'{v.REPO}/{WORKFLOW}@refs/heads/main',
                 GITHUB_RUN_NUMBER='1', GITHUB_RUN_ATTEMPT='1', RUNNER_ENVIRONMENT='github-hosted',
                 RUNNER_OS='Windows', RUNNER_ARCH='X64', ALLOCATION=ALLOCATION)
    need(all(context[k] == value for k,value in fixed.items()), 'Wrong execution host/workflow/opportunity')
    commit = context['REVIEWED_COMMIT']
    need(v.hexid(commit,40) and context['GITHUB_SHA'] == context['GITHUB_WORKFLOW_SHA'] == commit,
         'Unreviewed execution checkout/workflow')
    f = frozen()
    need(commit != f['harness_commit'], 'Old preparation is not the corrected execution version')
    run = context['GITHUB_RUN_ID']
    need(isinstance(run,str) and re.fullmatch('[1-9][0-9]*',run) is not None and
         run not in (f['run'],FAILURE_RUN), 'Old or invalid execution run')
    return context


def check_repo(repo):
    v.check_repo(repo)
    for name,digest in CORE_PINS.items():
        need(v.sha(v.canonical((repo/name).read_bytes())) == digest, 'Shared/consumed source changed: '+name)
    need(v.sha(v.canonical((repo/IDENTITY).read_bytes())) == IDENTITY_SHA, 'Identity changed')
    need(v.file_sha(repo/FAILURE) == FAILURE_SHA, 'Consumed failure changed')


def check_preparation(inputs):
    """Authenticate ORIGINAL bytes separately; never pass a fake measure request."""
    f = frozen()
    files = {p.relative_to(inputs).as_posix():p for p in inputs.rglob('*') if p.is_file()}
    need(set(files) == set(f['members']), 'Prepared member inventory changed')
    for name,digest in f['members'].items():
        need(v.file_sha(files[name]) == digest, 'Prepared member changed: '+name)
    m = v.check_prepared(inputs,f['members']['manifest.json'])
    prep = load(inputs/'request.json')
    need(prep['REVIEWED_COMMIT'] == m['harness_commit'] == f['harness_commit'] and
         prep['GITHUB_RUN_ID'] == m['prepare_run'] == f['run'], 'Old preparation source/run mismatch')
    return m


def unpack(archive, inputs):
    f = frozen()
    need(archive.stat().st_size == f['archive_bytes'] and v.file_sha(archive) == f['archive_sha256'],
         'Wrong original preparation ZIP')
    with ZipFile(archive) as z:
        v.safe_members(z)
        need(set(z.namelist()) == set(f['members']) and
             sum(i.file_size for i in z.infolist()) == f['expanded_bytes'], 'Wrong original ZIP inventory')
    v.unpack(archive,f['archive_sha256'],inputs)
    return check_preparation(inputs)


def execution_source(root, context):
    path = root/'source-execution.zip'
    need(path.stat().st_size <= 64*1024*1024, 'Execution source archive budget')
    with ZipFile(path) as z:
        v.safe_members(z)
        need(z.comment == context['REVIEWED_COMMIT'].encode('ascii') and
             sum(i.file_size for i in z.infolist()) <= 128*1024*1024 and z.testzip() is None,
             'Execution source snapshot identity/integrity')
        for name in (*v.PINS,*CORE_PINS,IDENTITY,FAILURE,WORKFLOW,
                     'tests/native/v9_noop_rebound.py','tests/native/run_v9_noop_rebound.ps1'):
            archived = z.read(name)
            local = (ROOT/name).read_bytes()
            need((archived == local) if name == FAILURE else (v.canonical(archived) == v.canonical(local)),
                 'Execution critical source differs: '+name)
    return v.file_sha(path)


def binding(context, source_sha256):
    admit(context)
    return dict(schema=1, execution_commit=context['REVIEWED_COMMIT'], execution_run=context['GITHUB_RUN_ID'],
                execution_workflow=WORKFLOW, execution_source_sha256=source_sha256, protocol=protocol())


def expected_plan(context, inputs, evidence_root, source_sha256):
    f = frozen()
    return dict(protocol=protocol(), binding=binding(context,source_sha256),
                root=evidence_root, input_root=str(inputs.absolute()),
                binaries={s:f['members'][f'bin/{s}.exe'] for s in v.SOURCES},
                preparation_harness_commit=f['harness_commit'], execution_harness_commit=context['REVIEWED_COMMIT'],
                input_manifest_sha256=f['members']['manifest.json'])


def copy_new(source, destination):
    with source.open('rb') as src, destination.open('xb') as dst:
        shutil.copyfileobj(src,dst)


def prepare_measure(root, inputs, repo):
    check_repo(repo)
    context = admit(load(root/'request.json'))
    m = check_preparation(inputs)
    need({p.name for p in root.iterdir()} == {'request.json','source-execution.zip'}, 'Fresh execution evidence required')
    # Keep distinct copies, byte-for-byte: these remain ORIGINAL requests/manifest.
    copy_new(inputs/'request.json',root/'preparation-request.json')
    copy_new(inputs/'manifest.json',root/'preparation-manifest.json')
    copy_new(repo/FAILURE,root/'consumed-measurement.zip')
    source_sha256 = execution_source(root,context)
    write(root/'binding.json',binding(context,source_sha256))
    plan = expected_plan(context,inputs,str(root.absolute()),source_sha256)
    need(plan['binaries'] == m['binaries'], 'Prepared binary identity mismatch')
    write(root/'plan.json',plan)
    for folder in ('calls','cache-projections','fixtures'):
        (root/folder).mkdir()
    return plan


def audit(root, inputs):
    check_repo(ROOT)
    context = admit(load(root/'request.json'))
    m = check_preparation(inputs)
    plan = load(root/'plan.json')
    # Recorded paths belong to the original Windows host, not this offline replay.
    source_sha256 = execution_source(root,context)
    expected = expected_plan(context,inputs,plan['root'],source_sha256)
    expected['input_root'] = plan['input_root']
    need(isinstance(plan['root'],str) and isinstance(plan['input_root'],str) and
         plan['root'] and plan['input_root'] and v.same(plan,expected), 'Execution plan changed')
    need(v.same(load(root/'binding.json'),binding(context,source_sha256)), 'Preparation/execution binding changed')
    for name,original in (('preparation-request.json','request.json'),('preparation-manifest.json','manifest.json')):
        need(v.file_sha(root/name) == frozen()['members'][original], 'Rewritten original preparation metadata')
    need(v.file_sha(root/'consumed-measurement.zip') == FAILURE_SHA, 'Missing/changed consumed failure')
    # Refuse incomplete runs before reading any absent call records.
    need(v.same(load(root/'completion.json'),dict(status='calls_complete_unreviewed',attempted=16,error=None,clears_hold=False)),
         'Stopped/incomplete rebound')
    # Keep all source files present and unchanged, not merely equal absent manifests.
    for row in v.rows():
        for part in ('before','after'):
            manifest = v.boundary.manifest(load(root/'calls'/f"{row['sequence']:02d}.{part}.json"))
            for name,data in v.boundary.SOURCES.items():
                item = manifest.get(name)
                need(item is not None and item['size'] == len(data) and item['sha256'] == v.sha(data), 'fixture source changed')
    result = v.audit_calls(root,plan,m)
    result.update(scope='V9 rebound on frozen preparation; not original #701',
                  binding=binding(context,source_sha256), previous_study_calls=121, completed_study_total=137,
                  new_preparation_calls=0)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=('plan','admit','unpack','check-preparation','prepare-measure','audit'))
    p.add_argument('--root',type=Path); p.add_argument('--inputs',type=Path); p.add_argument('--archive',type=Path)
    p.add_argument('--repo',type=Path,default=ROOT); p.add_argument('--output',type=Path)
    a = p.parse_args()
    try:
        if a.command == 'plan': result = protocol()
        elif a.command == 'admit':
            result = admit(load(a.root/'request.json')); check_repo(a.repo)
            need(v.same(result,{k:os.environ.get(k) for k in KEYS}), 'Saved/live execution mismatch')
        elif a.command == 'unpack': result = unpack(a.archive,a.inputs)
        elif a.command == 'check-preparation':
            check_repo(a.repo); m = check_preparation(a.inputs)
            result = dict(status='fixed_preparation_verified_unmeasured',preparation_harness=m['harness_commit'],
                          binaries=m['binaries'],new_mqb_calls=0,clears_hold=False)
        elif a.command == 'prepare-measure': result = prepare_measure(a.root,a.inputs,a.repo)
        else: result = audit(a.root,a.inputs)
        if a.output: write(a.output,result)
        return 1 if a.command == 'audit' and result['crossed'] else 0
    except (ValueError,OSError,KeyError,TypeError,BadZipFile,subprocess.CalledProcessError) as exc:
        if a.output and not a.output.exists():
            write(a.output,dict(status='INVALID',error=str(exc),clears_hold=False,cause=None))
        print('REFUSED: '+str(exc),file=sys.stderr)
        return 2

if __name__ == '__main__': sys.exit(main())
