"""Bounded historical-checkpoint adjacency test; #164 comment5711255636.

Only full-history serialization/overwrite differs between arms. Both retain
append-only call snapshots and all original diagnostics. Not a release gate.
"""
from __future__ import annotations
import argparse
import ctypes as C
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import sys
import tempfile
import time
import traceback
import types
import zipfile

BASE = '55f57a84ad938da10d0e28b4578cd1aef6d7f903'
BRANCH = 'codex/private129-checkpoint-once-20260917'
PRIOR_HASH = '413a0371435301af9333972212f1a8a4cb3135d6b1b58fe3b7dc70edac7691ee'
CONTROLLER_HASH = '8dc5732864f81d80d451a747f02448a6138dfa09dd436b3b17483f98e2a39f43'
ARCHIVE_HASH = 'd70a0478c8b060e1da1b64e1361ec9ee69978277eed3d2c671149cff372c4530'
HELPER_HASH = '96970354335668a161805c04512985ef3775f59f96da60e26f8ab22d96196b8f'
ALLOWED = {'.github/workflows/private129-checkpoint-contrast.yml',
           'tests/native/private129_checkpoint_contrast.py'}
ARMS, SIDES = ('full', 'append'), ('baseline', 'candidate')
MAX_CALLS = 54


def require(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def allocated(env):
    expected = dict(GITHUB_REPOSITORY='Iviesever/msvc-quick-build', GITHUB_REF='refs/heads/'+BRANCH,
                    GITHUB_RUN_NUMBER='1', GITHUB_RUN_ATTEMPT='1', GITHUB_ACTIONS='true',
                    RUNNER_ENVIRONMENT='github-hosted')
    return all(env.get(k) == v for k,v in expected.items())


def plan():
    rows = []
    for phase in ('prime', 'pre'):
        for side in SIDES:
            rows.append(dict(phase=phase, side=side, jobs='auto', block=None, arm=None, prefix_count=None))
    for block in range(1,13):
        arms = ARMS if block % 2 else ARMS[::-1]
        sides = SIDES if (block-1) % 4 in (0,3) else SIDES[::-1]
        for arm in arms:
            for position, side in enumerate(sides):
                rows.append(dict(phase='score', side=side, jobs='auto', block=block, arm=arm,
                                 prefix_count=438 + 2*(block-1) + position))
    for side in SIDES:
        rows.append(dict(phase='post', side=side, jobs='auto', block=None, arm=None, prefix_count=None))
    return rows


def load_inputs(path, out):
    raw = path.read_bytes()
    require(digest(raw) == PRIOR_HASH, 'wrong prior artifact')
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        require(z.testzip() is None, 'prior CRC failure')
        source = z.read('evidence/controller-source.zip')
        cumulative = z.read('original-cumulative.zip')
    require(digest(cumulative) == ARCHIVE_HASH, 'wrong cumulative archive')
    with zipfile.ZipFile(io.BytesIO(source)) as z:
        code = z.read('tests/native/private129_jobs_contrast.py').replace(b'\r\n',b'\n')
    require(digest(code) == CONTROLLER_HASH, 'wrong retained recorder')
    module_path = out/'retained_controller.py'
    module_path.write_bytes(code)
    spec = importlib.util.spec_from_file_location('checkpoint_retained_controller',module_path)
    q = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = q
    spec.loader.exec_module(q)
    q.self_test()
    cumulative_path = out/'original-cumulative.zip'
    cumulative_path.write_bytes(cumulative)
    h, helper_hash = q.extract_pinned(cumulative_path,out/'original')
    require(helper_hash == HELPER_HASH, 'original reporting helper changed')
    h.self_test()
    with zipfile.ZipFile(io.BytesIO(cumulative)) as z:
        calls_raw = z.read('evidence/calls.json')
    old = json.loads(calls_raw)
    require(len(old) == 1446 and old[438]['label'] == 'private-default-jauto-1-baseline', 'historical prefix boundary')
    require(json.dumps(old,indent=2).replace('\n','\r\n').encode('utf-8') == calls_raw, 'original JSON bytes differ')
    prefixes = {n:old[:n] for n in range(438,462)}
    manifests = {}
    for n, rows in prefixes.items():
        data = json.dumps(rows,indent=2).replace('\n','\r\n').encode('utf-8')
        manifests[str(n)] = dict(records=n, logical_bytes=len(data), sha256=digest(data))
    dump(out/'prefixes.json',manifests)
    dump(out/'pinned-inputs.json',dict(prior=PRIOR_HASH,cumulative=ARCHIVE_HASH,controller=CONTROLLER_HASH,
         reporting=HELPER_HASH,old_calls_sha256=digest(calls_raw),binaries=q.EXE_HASHES))
    return q,h,prefixes,manifests


def volume(path):
    k = C.WinDLL('kernel32',use_last_error=True)
    k.GetVolumePathNameW.argtypes=[C.c_wchar_p,C.c_wchar_p,C.c_uint32]
    k.GetVolumePathNameW.restype=C.c_int
    k.GetVolumeNameForVolumeMountPointW.argtypes=[C.c_wchar_p,C.c_wchar_p,C.c_uint32]
    k.GetVolumeNameForVolumeMountPointW.restype=C.c_int
    k.GetVolumeInformationW.argtypes=[C.c_wchar_p,C.c_wchar_p,C.c_uint32,C.POINTER(C.c_uint32),
          C.POINTER(C.c_uint32),C.POINTER(C.c_uint32),C.c_wchar_p,C.c_uint32]
    k.GetVolumeInformationW.restype=C.c_int
    mount,guid,fs,label=(C.create_unicode_buffer(1024) for _ in range(4))
    serial,component,flags=C.c_uint32(),C.c_uint32(),C.c_uint32()
    result=dict(path=str(path),errors={})
    for name,func,args in [
      ('mount',k.GetVolumePathNameW,(str(path),mount,1024)),
      ('guid',k.GetVolumeNameForVolumeMountPointW,(mount,guid,1024)),
      ('information',k.GetVolumeInformationW,(mount,label,1024,C.byref(serial),C.byref(component),C.byref(flags),fs,1024))]:
        C.set_last_error(0)
        if not func(*args):
            result['errors'][name]=C.get_last_error()
            return result
    result.update(mount=mount.value,guid=guid.value,filesystem=fs.value,serial=serial.value)
    return result


def make_recorder(q,h,prefixes,manifests):
    class AppendRecorder(q.Recorder):
        def __init__(self,*args):
            super().__init__(*args)
            self.event_count=0
            self.intervened=set()
            self.replays=0
            (self.root/'replay').mkdir()
        def checkpoint(self):
            # Persist one immutable snapshot, never rewrite the complete new history.
            with (self.root/'call-events.jsonl').open('a',encoding='utf-8',newline='\n') as f:
                f.write(json.dumps(dict(event=self.event_count,row=self.rows[-1]),allow_nan=False)+'\n')
            self.event_count+=1
            row=self.rows[-1]
            if row['index'] in self.intervened:
                return
            self.intervened.add(row['index'])
            if row['phase']!='score':
                return
            n=row['prefix_count']
            row['checkpoint_intervention']=dict(attempted=row['arm']=='full',prefix_count=n,
                expected=manifests[str(n)],wall_ms=None,cpu_ms=None,completed=False)
            if row['arm']=='full':
                self.replays+=1  # Reserve the bounded action before executing it.
                require(self.replays<=24,'checkpoint budget exhausted')
                replay=types.SimpleNamespace(root=self.root/'replay',calls=prefixes[n])
                started=time.perf_counter_ns()
                cpu_started=time.process_time_ns()
                try:
                    h.Recorder.checkpoint(replay)
                except Exception as error:
                    row['checkpoint_intervention']['error']=repr(error)
                    raise
                finally:
                    row['checkpoint_intervention'].update(cpu_ms=(time.process_time_ns()-cpu_started)/1e6,
                        wall_ms=(time.perf_counter_ns()-started)/1e6)
                row['checkpoint_intervention']['completed']=True
            # Returning to the unchanged Recorder.run starts its MQB timer only now.
    return AppendRecorder


def summarize(h,rows):
    require(len(rows)==MAX_CALLS,'incomplete experiment')
    result,pairs_by_arm={},{}
    for arm in ARMS:
        pairs=[]
        for block in range(1,13):
            selected=[r for r in rows if r['phase']=='score' and r['arm']==arm and r['block']==block]
            require(len(selected)==2 and {r['side'] for r in selected}==set(SIDES),'duplicate or missing pair')
            require(all(r['exit_code']==0 for r in selected),'failed measurement')
            pairs.append(dict(block=block,**{r['side']:r for r in selected}))
        result[arm]=h.summarize(pairs)
        pairs_by_arm[arm]=pairs
    interaction=[a-b for a,b in zip(result['full']['paired_deltas_ms'],result['append']['paired_deltas_ms'])]
    within={}
    for side in SIDES:
        values=[f[side]['external_ms']-a[side]['external_ms']
                for f,a in zip(pairs_by_arm['full'],pairs_by_arm['append'])]
        within[side]=dict(full_minus_append_ms=values,median_ms=statistics.median(values))
    replays=[r['checkpoint_intervention'] for r in rows if r['phase']=='score' and r['arm']=='full']
    require(len(replays)==24 and all(r['completed'] for r in replays),'missing checkpoint action')
    return dict(by_arm=result,block_interaction_ms=interaction,interaction_median_ms=statistics.median(interaction),
        same_version=within,checkpoint_wall_ms=[r['wall_ms'] for r in replays],
        checkpoint_cpu_ms=[r['cpu_ms'] for r in replays],
        logical_rewrite_bytes=sum(r['expected']['logical_bytes'] for r in replays),
        physical_bytes_measured=False,carryover_excluded=False,historical_flag_cleared=False,
        cause_proven=False,release_authorized=False,new_acceptance_threshold=None)


def self_test():
    rows=plan()
    require(len(rows)==54 and sum(r['phase']=='score' for r in rows)==48,'budget')
    for arm in ARMS:
        combinations=[]
        for block in range(1,13):
            allrows=[r for r in rows if r['phase']=='score' and r['block']==block]
            selected=[r for r in allrows if r['arm']==arm]
            combinations.append((tuple(r['side'] for r in selected),allrows[0]['arm']==arm))
        require(all(combinations.count((s,first))==3 for s in (SIDES,SIDES[::-1]) for first in (False,True)),'balance')
    require(sorted(r['prefix_count'] for r in rows if r['arm']=='full')==list(range(438,462)),'prefix allocation')
    valid=dict(GITHUB_REPOSITORY='Iviesever/msvc-quick-build',GITHUB_REF='refs/heads/'+BRANCH,
        GITHUB_RUN_NUMBER='1',GITHUB_RUN_ATTEMPT='1',GITHUB_ACTIONS='true',RUNNER_ENVIRONMENT='github-hosted')
    require(allocated(valid),'valid allocation')
    for k in valid:
        for bad in ('','2','01','false'):
            require(not allocated({**valid,k:bad}),'unallocated context')
    return dict(passed=True,max_mqb=54,max_rewrites=24,Windows_execution=False)


def execute(args):
    require(os.name=='nt' and allocated(os.environ),'not the unique allocated hosted run')
    out=args.output.resolve()
    require(out.drive.upper()=='D:' and not out.exists(),'new D workspace output required')
    out.mkdir(parents=True)
    rec=None
    try:
        q,h,prefixes,manifests=load_inputs(args.input.resolve(),out)
        head=q.git('rev-parse','HEAD')
        require(head==os.environ['GITHUB_SHA'] and q.git('rev-parse','HEAD^')==BASE,'wrong source parent')
        require(set(q.git('diff','--name-only',BASE,head).splitlines())==ALLOWED,'unexpected source change')
        require(q.git('status','--porcelain','--untracked-files=no')=='','tracked source dirty')
        require(Path('VERSION').read_text().strip()=='5.5.0','version changed')
        q.subprocess.run(['git','archive','-o',str(out/'controller-source.zip'),head],check=True)
        dump(out/'identity.json',dict(head=head,base=BASE,tree=q.git('rev-parse','HEAD^{tree}'),
            run_id=os.environ['GITHUB_RUN_ID'],run_number=1,attempt=1,preregistration=5711255636,
            image=os.environ.get('ImageVersion'),os=q.platform.platform(),python=sys.version))
        dump(out/'environment.json',{k:os.environ.get(k) for k in
            ('TEMP','TMP','PATH','CL','_CL_','INCLUDE','LIB','LIBPATH','NUMBER_OF_PROCESSORS')})
        temp=Path(tempfile.gettempdir()).resolve()
        require(temp.drive.upper()=='C:','C system temp required')
        parent=Path(tempfile.mkdtemp(prefix='mqb-cumulative-',dir=temp)).resolve()
        case=h.fixture(parent,'private',129)
        for source in sorted(case.root.glob('*.cpp')):
            source.with_suffix('.hpp').write_bytes((case.root/'common.hpp').read_bytes())
            source.write_text(source.read_text(encoding='utf-8').replace('common.hpp',source.with_suffix('.hpp').name),encoding='utf-8')
        for ancestor in (case.root,*case.root.parents):
            require(not os.lstat(ancestor).st_file_attributes & 0x400,'reparse project path')
        volumes={label:volume(p) for label,p in [('project',case.root),('output',out)]}
        dump(out/'volumes.json',volumes)
        require(all(not v['errors'] and v.get('guid') for v in volumes.values()),'volume identity unavailable')
        dump(out/'plan.json',plan())
        q.plan,q.MAX_CALLS=plan,MAX_CALLS
        rec=make_recorder(q,h,prefixes,manifests)(out,h,case,q.NativeMetrics())
        before=None
        for item in plan():
            row=rec.run(item,out/'original'/item['side']/'mqb.exe')
            if item['phase']=='prime':
                text=h.normalized((out/row['stdout_file']).read_bytes())
                count=129 if item['side']=='baseline' else 0
                require(len(re.findall(rb'^\[compile\] ',text,re.M))==count,'prime compile count')
                require(len(re.findall(rb'^\[link\] ',text,re.M))==int(count>0),'prime link count')
            if item['phase'] in ('pre','post') and item['side']=='candidate':
                require(h.semantic_counters(rec.rows[-2])==h.semantic_counters(row),'audit counter mismatch')
            if len(rec.rows)==4:
                before=h.state(case)
                dump(out/'metadata-before.json',before)
            if len(rec.rows)==52:
                after=h.state(case)
                dump(out/'metadata-after-scores.json',after)
                require(before==after,'scored calls changed project state')
        after=h.state(case)
        dump(out/'metadata-after-audits.json',after)
        require(before==after,'audit changed project state')
        dump(out/'summary.json',summarize(h,rec.rows))
        last=(out/'replay/calls.json').read_bytes()
        require(digest(last)==manifests['461']['sha256'],'last replay byte mismatch')
        dump(out/'last-replay.json',dict(prefix_count=461,bytes=len(last),sha256=digest(last)))
        dump(out/'actual-tools.json',q.actual_tools(rec.rows,out))
        contents=q.inventory(case.root)
        dump(out/'post-content-hashes.json',contents)
        shutil.copytree(case.root,out/'fixture')
        require({r['path']:r['sha256'] for r in q.inventory(out/'fixture')}==
                {r['path']:r['sha256'] for r in contents},'copied contents differ')
        for side,sha in q.EXE_HASHES.items():
            require(digest((out/'original'/side/'mqb.exe').read_bytes())==sha,'EXE changed')
        require(digest(args.input.read_bytes())==PRIOR_HASH,'prior artifact changed')
        dump(out/'completion.json',dict(completed=True,mqb_calls=54,checkpoint_calls=24,
            new_mqb_builds=0,new_etw=0,fixture_executions=0,historical_flag_cleared=False,release_authorized=False))
    except Exception:
        (out/'failure.txt').write_text(traceback.format_exc(),encoding='utf-8')
        raise
    finally:
        if rec is not None:
            dump(out/'calls.json',rec.rows)
            dump(out/'budget.json',dict(maximum_mqb=54,reserved_mqb=len(rec.rows),
                observed_pids=sum('pid' in r for r in rec.rows),remaining_not_run=54-len(rec.rows),
                max_rewrites=24,attempted_rewrites=rec.replays))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test',action='store_true')
    p.add_argument('--input',type=Path)
    p.add_argument('--output',type=Path)
    args=p.parse_args()
    result=self_test()
    if args.self_test:
        print(json.dumps(result,indent=2))
        return
    require(args.input is not None and args.output is not None,'missing input/output')
    execute(args)


if __name__=='__main__':
    main()
