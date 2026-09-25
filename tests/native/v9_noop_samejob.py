"""Single-job A2/B2 protocol. Offline checks never build, measure or grant allocation."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
from zipfile import BadZipFile, ZipFile

import v9_noop_validation as v

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = '.github/workflows/v9-noop-samejob.yml'
ALLOCATION = 'v9-reader-samejob-001'
KEYS = tuple(k for k in v.KEYS if k not in ('PHASE','PREP_RUN','PREP_ARTIFACT','PREP_SHA256','MANIFEST_SHA256')) + ('GITHUB_JOB','ACCEPT_AUTOMATIC_GATE')
SEED_SHA = 'f58d78066a2ec6c4d174e39fcb9c748776cdc4a6137a0d5a592a216b3eba6dd0'
# Pin the *complete* inherited helpers, not merely the names imported via AST.
PINS = dict(v.PINS, **{
    'tests/native/v9_noop_validation.py': 'd1621c5fe9a00ea32d23a494a97b393c7b40e98594cab7a7ef3756da84c395b6',
    'tests/native/run_v9_noop_validation.ps1': '00aae69c629ae6c1c08a9bd86766b68aea2c4b7db109e69637e551a7b46afa05',
})
NEW_FILES = (WORKFLOW, 'tests/native/v9_noop_samejob.py', 'tests/native/run_v9_noop_samejob.ps1')
HISTORY = dict(study_calls=121, preparation_calls=5,
    original_preparation_run='36106155413', original_preparation_artifact='10852045267',
    original_preparation_sha256='b01cb62dafd12a1b5e3d812d6165a712042d7db8a5391f76f4680a20895f3282',
    measurement001=dict(run='36107949309', artifact='10851277845', calls=1, noop=0,
        sha256='c0ac5b61a9e3e0ca7956794cf221a1dd4c2bb13a10ceeb916f58f01f14a9c30b'),
    measurement002=dict(run='36156676599', artifact='10873757486', calls=0, noop=0,
        sha256='92553eb9ec9be76ce6de5621593ab44d1a53a6475dd089d88624de02e45345ab'))
need, load, write = v.need, v.load, v.write


def protocol():
    return dict(schema=1, purpose='New same-job A2/B2; not original #701 or a refill',
        sources={s:dict(p) for s,p in v.SOURCES.items()}, argv=list(v.boundary.ARGV), rows=v.rows(),
        binary_labels={'baseline':'A2','candidate':'B2'}, preparation_mqb_ceiling=5, study_mqb_ceiling=16,
        history=json.loads(json.dumps(HISTORY)), proposed_study_total=137, proposed_preparation_total=10,
        allocation=ALLOCATION, execution_allocated=False, automatic_intermediate_gate=True,
        human_acceptance='after execution; source and automatic gate reviewed before allocation',
        clears_hold=False, cause=None)


def admit(c):
    need(isinstance(c,dict) and set(c)==set(KEYS), 'Missing/extra request fields')
    fixed=dict(GITHUB_REPOSITORY=v.REPO,GITHUB_ACTIONS='true',GITHUB_EVENT_NAME='workflow_dispatch',
        GITHUB_REF='refs/heads/main',GITHUB_WORKFLOW_REF=f'{v.REPO}/{WORKFLOW}@refs/heads/main',
        GITHUB_RUN_NUMBER='1',GITHUB_RUN_ATTEMPT='1',RUNNER_ENVIRONMENT='github-hosted',
        RUNNER_OS='Windows',RUNNER_ARCH='X64',GITHUB_JOB='validation',ALLOCATION=ALLOCATION,
        ACCEPT_AUTOMATIC_GATE='true')
    need(all(c[k]==x for k,x in fixed.items()), 'Wrong workflow/host/opportunity or missing automatic-gate consent')
    commit=c['REVIEWED_COMMIT']; run=c['GITHUB_RUN_ID']
    need(v.hexid(commit,40) and c['GITHUB_SHA']==c['GITHUB_WORKFLOW_SHA']==commit, 'Unreviewed execution revision')
    need(isinstance(run,str) and re.fullmatch('[1-9][0-9]*',run) is not None and
         run not in (HISTORY['original_preparation_run'],HISTORY['measurement001']['run'],HISTORY['measurement002']['run']),
         'Old/invalid run identity')
    return c


def check_repo(repo):
    for name,digest in PINS.items():
        need(v.sha(v.canonical((repo/name).read_bytes()))==digest, 'Inherited source changed: '+name)


def git_hash(kind, data):
    return hashlib.sha1(kind.encode()+b' '+str(len(data)).encode()+b'\0'+data).digest()


def source_archive(path, commit, expected_tree=None):
    """Reconstruct the Git tree from bounded raw git-archive bytes. No extraction."""
    need(path.stat().st_size<=64*1024*1024 and not path.is_symlink(), 'Source ZIP budget/type')
    with ZipFile(path) as z:
        v.safe_members(z)
        need(len(z.infolist())<=2000 and sum(i.file_size for i in z.infolist())<=128*1024*1024 and
             z.comment==commit.encode('ascii') and z.testzip() is None, 'Wrong source ZIP identity/integrity')
        root={}; files={}
        for info in z.infolist():
            if info.is_dir(): continue
            mode=info.external_attr>>16
            need(not mode or stat.S_IFMT(mode) in (0,stat.S_IFREG), 'Non-regular source entry')
            raw=z.read(info)
            # Preserve the explicit binary .bat and binary fixtures; normalize only text.
            data=raw if info.filename=='install.bat' or b'\0' in raw or info.filename.endswith('.zip') else v.canonical(raw)
            files[info.filename]=data
            node=root; parts=info.filename.split('/')
            for part in parts[:-1]:
                if part not in node: node[part]={}
                need(isinstance(node[part],dict), 'Source file/directory alias')
                node=node[part]
            need(parts[-1] not in node, 'Source file/directory alias')
            node[parts[-1]]=(b'100755' if mode & 0o111 else b'100644',git_hash('blob',data))
        def tree(node):
            data=b''
            for name,value in sorted(node.items(),key=lambda p:(p[0]+('/' if isinstance(p[1],dict) else '')).encode('utf-8')):
                mode,h=(b'40000',tree(value)) if isinstance(value,dict) else value
                data+=mode+b' '+name.encode('utf-8')+b'\0'+h
            return git_hash('tree',data)
        digest=tree(root).hex()
        if expected_tree is not None: need(digest==expected_tree,'Source tree changed')
    return dict(commit=commit,tree=digest,files=len(files),sha256=v.file_sha(path)),files


def execution(root, repo):
    check_repo(repo); c=admit(load(root/'request.json'))
    identity,files=source_archive(root/'source-execution.zip',c['REVIEWED_COMMIT'])
    for name in (*PINS,*NEW_FILES):
        need(files.get(name)==v.canonical((repo/name).read_bytes()),'Execution critical source differs: '+name)
    return c,identity


def preparation_members():
    return {'environment-before.json','environment-after.json','seed-identity.json','completion.json',
        'seed.log','baseline-build.log','candidate-build.log','source-baseline.zip','source-candidate.zip',
        'bin/baseline.exe','bin/candidate.exe'} | {f'{p}.{s}.json' for p in ('seed','baseline','candidate') for s in ('started','finished')}


def preparation(root, c, source):
    p=root/'preparation'
    files={x.relative_to(p).as_posix():x for x in p.rglob('*') if x.is_file()}
    need(set(files)-{'manifest.json'}==preparation_members(), 'Incomplete/extra preparation members')
    need(v.same(load(p/'completion.json'),dict(status='prepared_unmeasured',mqb_ceiling_admitted=5,error=None,clears_hold=False)),
         'Preparation stopped/incomplete')
    for phase,budget in (('seed',1),('baseline',2),('candidate',2)):
        need(v.same(load(p/f'{phase}.started.json'),dict(phase=phase,mqb_ceiling=budget)) and
             v.same(load(p/f'{phase}.finished.json'),dict(phase=phase,success=True)), 'Preparation helper incomplete')
    before=v.check_environment(load(p/'environment-before.json'))
    need(v.same(before,load(p/'environment-after.json')), 'Preparation environment changed')
    need(v.same(load(p/'seed-identity.json'),dict(sha256=SEED_SHA)), 'Wrong seed identity')
    sources={}
    for side,expected in v.SOURCES.items():
        sources[side],_=source_archive(p/f'source-{side}.zip',expected['commit'],expected['tree'])
        need(0<(p/'bin'/f'{side}.exe').stat().st_size<=64*1024*1024,'Invalid new binary size')
        need(0<(p/f'{side}-build.log').stat().st_size<=64*1024*1024,'Missing/oversized build log')
    need(0<(p/'seed.log').stat().st_size<=64*1024*1024,'Missing/oversized seed log')
    return dict(kind='single_job_new_pair_unmeasured',protocol=protocol(),execution=c,execution_source=source,
        environment=before,source_archives=sources,seed_exe_sha256=SEED_SHA,
        binaries={s:v.file_sha(p/'bin'/f'{s}.exe') for s in v.SOURCES},
        preparation_files={n:v.file_sha(files[n]) for n in sorted(preparation_members())},
        preparation_mqb_calls=5,performance_verified=False,clears_hold=False)


def freeze(root, repo):
    c,source=execution(root,repo); m=preparation(root,c,source)
    # New name, new identity and immutable raw file. Never rewrite an old manifest.
    write(root/'preparation/manifest.json',m)
    return check_frozen(root,repo)


def check_frozen(root, repo):
    c,source=execution(root,repo); expected=preparation(root,c,source)
    need(v.same(load(root/'preparation/manifest.json'),expected), 'New manifest changed')
    return expected


def plan_for(root_path, c, source, m, manifest_sha):
    return dict(protocol=protocol(),root=root_path+'/measurement',input_root=root_path+'/preparation',
        binaries=m['binaries'],input_manifest_sha256=manifest_sha,
        execution_commit=c['REVIEWED_COMMIT'],execution_run=c['GITHUB_RUN_ID'],execution_job=c['GITHUB_JOB'],
        execution_source=source)


def ready(root, repo):
    m=check_frozen(root,repo); c=m['execution']; source=m['execution_source']
    measure=root/'measurement';need(not measure.exists(),'Fresh measurement directory required')
    measure.mkdir()
    for name in ('calls','cache-projections','fixtures'): (measure/name).mkdir()
    plan=plan_for(str(root.absolute()).replace('\\','/'),c,source,m,v.file_sha(root/'preparation/manifest.json'))
    write(measure/'plan.json',plan)
    write(root/'ready.json',dict(status='new_pair_automatically_verified_not_human_accepted',plan=plan,clears_hold=False))
    return plan


def audit(root, repo):
    # Authenticate the new pair and execution before borrowing the old evidence-only auditor.
    m=check_frozen(root,repo); c=m['execution']; plan=load(root/'measurement/plan.json')
    need(isinstance(plan.get('root'),str) and plan['root'].endswith('/measurement'),'Invalid recorded root')
    expected=plan_for(plan['root'][:-12],c,m['execution_source'],m,v.file_sha(root/'preparation/manifest.json'))
    need(v.same(plan,expected),'Plan/binary/execution binding changed')
    need(v.same(load(root/'ready.json'),dict(status='new_pair_automatically_verified_not_human_accepted',plan=plan,clears_hold=False)),
         'Missing automatic intermediate gate')
    need(v.same(load(root/'completion.json'),dict(status='same_job_complete_unreviewed',
        preparation_mqb_ceiling_admitted=5,study_attempted=16,error=None,clears_hold=False)), 'Stopped/incomplete same-job run')
    measure=root/'measurement'
    need(v.same(load(measure/'environment-before.json'),m['environment']), 'Prepare/measure environment changed')
    # The parent owns the same binary pins through these phase boundaries. No help/build probes here.
    for row in v.rows():
        for phase in ('before','after'):
            files=v.boundary.manifest(load(measure/'calls'/f"{row['sequence']:02d}.{phase}.json"))
            for name,data in v.boundary.SOURCES.items():
                item=files.get(name)
                need(item is not None and item['size']==len(data) and item['sha256']==v.sha(data),'Fixture source changed')
    result=v.audit_calls(measure,plan,m)
    result.update(scope='New same-job A2/B2; does not clear original #701',history=protocol()['history'],
        execution_commit=c['REVIEWED_COMMIT'],execution_run=c['GITHUB_RUN_ID'],
        new_preparation_calls=5,completed_preparation_total=10,completed_study_total=137,
        automatic_intermediate_gate=True,human_acceptance_pending=True)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=('plan','admit','freeze','ready','audit'))
    p.add_argument('--root',type=Path);p.add_argument('--repo',type=Path,default=ROOT);p.add_argument('--output',type=Path)
    a=p.parse_args()
    try:
        if a.command=='plan': result=protocol()
        elif a.command=='admit':
            result=admit(load(a.root/'request.json'));check_repo(a.repo)
            need(v.same(result,{k:os.environ.get(k) for k in KEYS}),'Saved/live request differs')
            def git(*args):return subprocess.check_output(['git','-C',str(a.repo),*args],text=True).strip()
            need(git('rev-parse','HEAD')==result['REVIEWED_COMMIT'] and
                 not git('status','--porcelain','--untracked-files=no'),'Wrong/dirty checkout')
        elif a.command=='freeze':result=freeze(a.root,a.repo)
        elif a.command=='ready':result=ready(a.root,a.repo)
        else:result=audit(a.root,a.repo)
        if a.output:write(a.output,result)
        return 1 if a.command=='audit' and result['crossed'] else 0
    except (ValueError,OSError,KeyError,TypeError,BadZipFile,subprocess.CalledProcessError) as exc:
        if a.output and not a.output.exists():write(a.output,dict(status='INVALID',error=str(exc),clears_hold=False,cause=None))
        print('REFUSED: '+str(exc),file=sys.stderr);return 2

if __name__=='__main__':sys.exit(main())
