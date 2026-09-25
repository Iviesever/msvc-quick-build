"""Fixed V9 before/after validation. Offline functions never execute MQB.

The manual entry has separate preparation and measurement opportunities. Installing
this code allocates neither. A valid journal or non-crossed gate is not a release.
"""
from __future__ import annotations
import argparse
from decimal import Decimal, localcontext
from fractions import Fraction
import hashlib
import json
import ntpath
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
from zipfile import ZipFile

import check_external_noop_gate as gate
import external_noop_boundary as boundary

REPO = 'Iviesever/msvc-quick-build'
WORKFLOW = '.github/workflows/v9-noop-validation.yml'
SOURCES = {
    'baseline': {'commit': 'ed76d13df14acd680d5b523a27aa52fc99a1b09c',
                 'tree': '3258bd2f21c3982b8d9705ee5d50e56a02de5c0d'},
    'candidate': {'commit': 'fea79539ba3cdb55fddf36158877bae961627c01',
                  'tree': 'c7382dd4f6b33fa687a31b1689d199e23a120491'},
}
PINS = {
    'tests/native/acquire_seed.ps1': 'eea47140fb535cc77dfaf9bbf70a738861a77475f36615dcc28f4282256116f1',
    'tests/native/build_mqb.ps1': 'd6c90d2e9785f97fd0a176117c26841c3624699f5e7cfb7931d20e80a37e1826',
    'tests/native/assert_cpp_layout.ps1': '392a1a938d26cdf0f4320587519fee9be05f52978217774a8b23f7528f18ba68',
    'tests/native/collect_external_noop_boundary.ps1': '4b99ee4b1ad4de3608d3079175ff47bbd1de74e8f80dad715ac6b303698f9ebf',
    'tests/native/check_external_noop_gate.py': 'eec7f65d218aede93cda5030c9c10865eaf535075ba58b136f69dd02715b5bb7',
    'tests/native/external_noop_boundary.py': 'daad65d07b52edda2fa5a3f07c53c8c123b357a8bfe09039ffb151c43a45fee9',
}
KEYS = ('GITHUB_REPOSITORY','GITHUB_ACTIONS','GITHUB_EVENT_NAME','GITHUB_REF',
        'GITHUB_SHA','GITHUB_WORKFLOW_SHA','GITHUB_WORKFLOW_REF','GITHUB_RUN_ID',
        'GITHUB_RUN_NUMBER','GITHUB_RUN_ATTEMPT','RUNNER_ENVIRONMENT','RUNNER_OS',
        'RUNNER_ARCH','REVIEWED_COMMIT','PHASE','ALLOCATION','PREP_RUN',
        'PREP_ARTIFACT','PREP_SHA256','MANIFEST_SHA256')
# Application.cpp supplies this explicit CLI cache_file; the locator fallback
# msvc-<preference>-<host>-<target>.mqbcache is not used by this fixed command.
CACHE = '.mqb/cache/toolchain/vs-x64.cache'
need = gate.require
load = boundary.load
write = boundary.write_new

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def file_sha(path: Path) -> str:
    need(path.is_file() and not path.is_symlink(), 'Expected ordinary file')
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def canonical(data: bytes) -> bytes:
    return data.replace(b'\r\n', b'\n')

def same(a, b) -> bool:
    # Do not let bool silently equal an expected integer.
    return json.dumps(a, sort_keys=True, allow_nan=False) == json.dumps(b, sort_keys=True, allow_nan=False)

def hexid(value, size=64):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{'+str(size)+'}', value) is not None

def rows():
    result = []
    for pair in range(1, 5):
        sides = ('baseline','candidate') if pair % 2 else ('candidate','baseline')
        for side in sides:
            for phase in ('prime','noop'):
                result.append(dict(sequence=len(result)+1, pair=pair, side=side,
                                   phase=phase, fixture=f'{pair}-{side}'))
    return result

def protocol():
    return dict(schema=1, purpose='V9 production integration; not original #701 replay',
                sources={s:dict(v) for s,v in SOURCES.items()}, argv=list(boundary.ARGV), rows=rows(),
                preparation_mqb_ceiling=5, study_mqb_ceiling=16,
                previous_study_calls=120, proposed_study_total=136,
                allocations={'prepare':'v9-reader-prepare-001','measure':'v9-reader-noop-001'},
                preparation_allocated=False, measurement_allocated=False,
                clears_hold=False, cause=None)

def admit(context):
    need(set(context) == set(KEYS), 'Missing/extra request fields')
    phase = context['PHASE']
    need(phase in ('prepare','measure'), 'Wrong phase')
    fixed = dict(GITHUB_REPOSITORY=REPO, GITHUB_ACTIONS='true', GITHUB_EVENT_NAME='workflow_dispatch',
                 GITHUB_REF='refs/heads/main', GITHUB_WORKFLOW_REF=f'{REPO}/{WORKFLOW}@refs/heads/main',
                 GITHUB_RUN_NUMBER='1' if phase == 'prepare' else '2', GITHUB_RUN_ATTEMPT='1',
                 RUNNER_ENVIRONMENT='github-hosted', RUNNER_OS='Windows', RUNNER_ARCH='X64',
                 ALLOCATION=protocol()['allocations'][phase])
    need(all(context[k] == v for k,v in fixed.items()), 'Wrong source/host/opportunity; never retry')
    commit = context['REVIEWED_COMMIT']
    need(hexid(commit,40) and context['GITHUB_SHA'] == context['GITHUB_WORKFLOW_SHA'] == commit,
         'Unreviewed workflow/checkout')
    need(re.fullmatch('[1-9][0-9]*', context['GITHUB_RUN_ID'] or '') is not None, 'Bad run ID')
    if phase == 'prepare':
        need(all(context[k] in ('',None) for k in ('PREP_RUN','PREP_ARTIFACT','PREP_SHA256','MANIFEST_SHA256')),
             'Preparation cannot accept a replacement input')
    else:
        need(all(re.fullmatch('[1-9][0-9]*', context[k] or '') for k in ('PREP_RUN','PREP_ARTIFACT')),
             'Missing frozen input run/artifact')
        need(context['PREP_RUN'] != context['GITHUB_RUN_ID'], 'Cannot read own result')
        need(hexid(context['PREP_SHA256']) and hexid(context['MANIFEST_SHA256']), 'Missing frozen hashes')
    return context

def check_repo(repo):
    for name, expected in PINS.items():
        need(sha(canonical((repo/name).read_bytes())) == expected, 'Retained helper changed: '+name)

def check_sources(root):
    for side, expected in SOURCES.items():
        p = root/side
        def git(*args):
            return subprocess.check_output(['git','-C',str(p),*args], text=True).strip()
        need(git('rev-parse','HEAD') == expected['commit'] and
             git('rev-parse','HEAD^{tree}') == expected['tree'], 'Wrong source revision')
        need(not git('status','--porcelain','--untracked-files=no'), 'Dirty tracked source')
    a,b = (root/side for side in SOURCES)
    for name in ('VERSION','cpp/mqb.json','tests/native/build_mqb.ps1','tests/native/acquire_seed.ps1'):
        need(canonical((a/name).read_bytes()) == canonical((b/name).read_bytes()), 'Build policy differs')
    need((a/'VERSION').read_text().strip() == '5.6.0', 'Unexpected version')

def safe_members(z):
    seen = set()
    for info in z.infolist():
        name = info.filename; p = PurePosixPath(name)
        need(info.orig_filename == name and name and not p.is_absolute() and
             all(x not in ('','.','..') for x in name.rstrip('/').split('/')) and
             '\\' not in name and ':' not in name and not stat.S_ISLNK(info.external_attr >> 16),
             'Unsafe ZIP member')
        need(all(not x.endswith(('.', ' ')) and not any(c in x for c in '<>"|?*') and
                 x.split('.')[0].upper() not in {'CON','PRN','AUX','NUL',*[f'COM{i}' for i in range(1,10)],*[f'LPT{i}' for i in range(1,10)]}
                 for x in p.parts), 'Windows alias member')
        need(name.casefold() not in seen, 'Duplicate ZIP member')
        seen.add(name.casefold())
    return seen

def unpack(archive, expected, destination):
    need(not destination.exists(), 'Fresh extraction required')
    need(archive.stat().st_size <= 64*1024*1024 and file_sha(archive) == expected, 'Wrong fixed artifact')
    with ZipFile(archive) as z:
        safe_members(z)
        need(len(z.infolist()) <= 100 and sum(i.file_size for i in z.infolist()) <= 128*1024*1024,
             'Input archive budget exceeded')
        need(z.testzip() is None, 'Corrupt ZIP')
        destination.mkdir(parents=True); z.extractall(destination)

def milliseconds(ticks, frequency):
    need(type(ticks) is int and ticks >= 0 and type(frequency) is int and 0 < frequency <= 10**12,
         'Invalid integer QPC')
    exact = Fraction(ticks*1000, frequency)
    with localcontext() as ctx:
        ctx.prec = 80
        value = Decimal(exact.numerator)/Decimal(exact.denominator)
    # A nonterminating conversion is not rounded through a strict gate.
    need(Fraction(value) == exact, 'QPC unit cannot be represented exactly by existing gate')
    return value

def check_environment(value):
    need(isinstance(value,dict) and value.get('image') and value.get('powershell'), 'Missing host image')
    need(isinstance(value.get('tools'),list) and 0 < len(value['tools']) <= 32, 'Missing tool inventory')
    seen = set()
    for item in value['tools']:
        root = item.get('root'); need(isinstance(root,str) and root not in seen, 'Duplicate tool root')
        seen.add(root)
        need(set(item['files']) == {'cl.exe','link.exe','lib.exe','c1xx.dll','c2.dll'}, 'Incomplete key tool set')
        need(all(hexid(v) for v in item['files'].values()), 'Invalid tool digest')
    need(isinstance(value.get('sdk_versions'),list) and value['sdk_versions'], 'Missing SDK inventory')
    need(hexid(value.get('ambient_sha256')), 'Missing ambient hash')
    return value

def check_prepared(root, manifest_sha=None, request=None):
    if manifest_sha is not None:
        need(hexid(manifest_sha) and file_sha(root/'manifest.json') == manifest_sha, 'Wrong frozen manifest')
    m = load(root/'manifest.json')
    need(m['kind'] == 'matched_builds_unmeasured' and same(m['protocol'],protocol()), 'Wrong prepared protocol')
    need(m['clears_hold'] is False and m['performance_verified'] is False, 'Invented preparation verdict')
    prep = admit(load(root/'request.json'))
    need(prep['PHASE'] == 'prepare' and m['harness_commit'] == prep['REVIEWED_COMMIT'], 'Wrong preparation request')
    need(m['prepare_run'] == prep['GITHUB_RUN_ID'], 'Preparation run mismatch')
    if request is not None:
        admit(request)
        need(request['PHASE'] == 'measure' and prep['REVIEWED_COMMIT'] == request['REVIEWED_COMMIT'] and
             prep['GITHUB_RUN_ID'] == request['PREP_RUN'], 'Unfrozen preparation source/run')
    need(set(m['binaries']) == set(SOURCES) and set(m['source_archives']) == set(SOURCES), 'Missing A/B')
    for side in SOURCES:
        need(hexid(m['binaries'][side]) and file_sha(root/'bin'/f'{side}.exe') == m['binaries'][side], 'Binary changed')
        need(file_sha(root/f'source-{side}.zip') == m['source_archives'][side], 'Source archive changed')
    need(m['preparation_mqb_calls'] == 5 and type(m['preparation_mqb_calls']) is int, 'Wrong preparation budget')
    need(same(load(root/'completion.json'),dict(status='prepared_unmeasured',mqb_ceiling_admitted=5,
              error=None,clears_hold=False)), 'Preparation stopped')
    for phase,budget in (('seed',1),('baseline',2),('candidate',2)):
        need(same(load(root/f'{phase}.started.json'),dict(phase=phase,mqb_ceiling=budget)), 'Missing preparation prefix')
        need(same(load(root/f'{phase}.finished.json'),dict(phase=phase,success=True)), 'Incomplete helper')
    check_environment(m['environment'])
    need(same(load(root/'environment-before.json'),m['environment']) and
         same(load(root/'environment-after.json'),m['environment']), 'Changed build environment')
    need(hexid(m['seed_exe_sha256']), 'Missing seed identity')
    return m

def freeze(root, repo):
    check_repo(repo)
    request = admit(load(root/'request.json')); need(request['PHASE']=='prepare','Wrong phase')
    before,after = load(root/'environment-before.json'),load(root/'environment-after.json')
    need(same(before,after),'Environment changed while building')
    m = dict(kind='matched_builds_unmeasured',protocol=protocol(),harness_commit=request['REVIEWED_COMMIT'],
             prepare_run=request['GITHUB_RUN_ID'],preparation_mqb_calls=5,
             environment=check_environment(before),seed_exe_sha256=load(root/'seed-identity.json')['sha256'],
             binaries={s:file_sha(root/'bin'/f'{s}.exe') for s in SOURCES},
             source_archives={s:file_sha(root/f'source-{s}.zip') for s in SOURCES},
             performance_verified=False,clears_hold=False)
    write(root/'manifest.json',m)
    return check_prepared(root)

def prepare_measure(root, inputs, repo):
    check_repo(repo); request = admit(load(root/'request.json'))
    m = check_prepared(inputs,request['MANIFEST_SHA256'],request)
    plan = dict(protocol=protocol(),root=str(root.absolute()),input_root=str(inputs.absolute()),binaries=m['binaries'],
                input_manifest_sha256=file_sha(inputs/'manifest.json'),harness_commit=request['REVIEWED_COMMIT'])
    write(root/'plan.json',plan)
    for folder in ('calls','cache-projections','fixtures'):
        (root/folder).mkdir()
    return plan

def sample_record(ms):
    return dict(scenario=gate.SCENARIO,iteration=1,measurement_source='external_stopwatch',timing_schema_version=0,
                total_ms=ms,attribution=None,counters=None,counter_breakdown=None,
                **{k:-1 for k in ('compile_hits','compile_misses','link_hits','link_misses','archive_hits','archive_misses')})

def validate_call(row, record, before, after, binaries):
    need(same(record['row'],row) and record['argv']==boundary.ARGV,'Unplanned call')
    need(record['executable_sha256']==binaries[row['side']] and record['dispatch_attempted'] is True,
         'Wrong execution identity')
    need(record['error'] is None and type(record['exit_code']) is int and record['exit_code']==0 and
         record['cleanup'] is None and record['root_times'] is None and record['clears_hold'] is False and
         record['output_format']=='powershell_merged_lines','Failed/altered call')
    q=record['clock']; keys=('outer_start','native_start','native_end','outer_end'); ticks=[q[k] for k in keys]
    need(all(type(t) is int and t>0 for t in ticks) and ticks==sorted(ticks),'Bad QPC order')
    primary=milliseconds(ticks[2]-ticks[1],q['frequency']); need(primary>0,'Empty interval')
    lines=record['output_lines']; need(isinstance(lines,list) and all(isinstance(v,str) for v in lines),'Missing output')
    need(not any('mqb.timings' in x for x in lines),'Timings must stay disabled')
    compiles=sum(x.startswith('[compile] ') for x in lines); links=sum(x.startswith('[link] ') for x in lines)
    b,a=boundary.manifest(before),boundary.manifest(after)
    need('.mqb/bin/timing_bench.exe' in a and CACHE in a,'Missing output or V9 cache')
    if row['phase']=='prime':
        need(set(b)==set(boundary.SOURCES) and compiles==2 and links==1,'Not a fresh two-source prime')
    else:
        need(compiles==links==0 and [x for x in lines if x.startswith('[up-to-date] ')] ==
             ['[up-to-date] 2 translation units','[up-to-date] timing_bench.exe'],'Not the fixed no-op')
        need(same(before,after),'No-op changed files')
    return dict(sequence=row['sequence'],row=row,primary_ms=primary,
                outer_ms=milliseconds(ticks[3]-ticks[0],q['frequency']))

def audit(root, inputs):
    request=admit(load(root/'request.json')); m=check_prepared(inputs,request['MANIFEST_SHA256'],request)
    plan=load(root/'plan.json')
    need(same(plan['protocol'],protocol()) and plan['binaries']==m['binaries'] and
         plan['harness_commit']==request['REVIEWED_COMMIT'] and
         plan['input_manifest_sha256']==request['MANIFEST_SHA256'],'Plan changed')
    return audit_calls(root, plan, m)

def audit_calls(root, plan, m):
    # Shared evidence-only audit. Caller must authenticate its own request/plan/preparation.
    need(same(load(root/'completion.json'),dict(status='calls_complete_unreviewed',attempted=16,error=None,clears_hold=False)),
         'Stopped/incomplete run')
    before_env=load(root/'environment-before.json'); after_env=load(root/'environment-after.json')
    # PATH hash is required constant within each host, not equal across two hosts.
    check_environment(before_env); need(same(before_env,after_env),'Environment changed during measurement')
    need(same({k:v for k,v in before_env.items() if k!='ambient_sha256'},
              {k:v for k,v in m['environment'].items() if k!='ambient_sha256'}),'Build/measure key tool or image mismatch')
    expected={f"{r['sequence']:02d}.{s}.json" for r in rows() for s in ('before','started','result','after')}
    need({p.name for p in (root/'calls').iterdir()}==expected,'Missing/extra call evidence')
    projections={r['fixture'] for r in rows()}
    need({p.name for p in (root/'cache-projections').iterdir()}=={x+'.json' for x in projections},'Missing/extra cache projections')
    samples=[]; previous_end=0; frequency=None; cache_hash=None
    for r in rows():
        stem=root/'calls'/f"{r['sequence']:02d}"
        data={s:load(Path(str(stem)+'.'+s+'.json')) for s in ('before','started','result','after')}
        rec=data['result']; start=data['started']; q=rec['clock']
        need(same(start['row'],r) and start['argv']==boundary.ARGV and
             start['executable_sha256']==m['binaries'][r['side']],'Wrong started evidence')
        need(ntpath.normcase(ntpath.normpath(start['cwd']))==ntpath.normcase(ntpath.normpath(plan['root']+'/fixtures/'+r['fixture'])),
             'Wrong fixture path')
        need(ntpath.normcase(ntpath.normpath(start['executable'])) == ntpath.normcase(ntpath.normpath(plan['input_root']+'/bin/'+r['side']+'.exe')),
             'Wrong executable path')
        need(q['outer_start']>previous_end,'Overlapping/out-of-order calls'); previous_end=q['outer_end']
        if frequency is None: frequency=q['frequency']
        need(type(q['frequency']) is int and q['frequency']==frequency,'Clock frequency changed')
        sample=validate_call(r,rec,data['before'],data['after'],m['binaries']); samples.append(sample)
        if r['phase']=='noop':
            prior=load(root/'calls'/f"{r['sequence']-1:02d}.after.json")
            need(same(prior,data['before']),'Fixture changed after prime')
        else:
            projection=load(root/'cache-projections'/(r['fixture']+'.json'))
            cache=boundary.manifest(data['after'])[CACHE]
            need(projection['sha256']==cache['sha256'] and type(projection['bytes']) is int and projection['bytes']==cache['size'] and
                 projection['schema']=='MQB_TOOLCHAIN_CACHE_V9','Cache projection mismatch')
            need(projection['root'] in {x['root'] for x in before_env['tools']},'Unexpected selected toolchain')
            if cache_hash is None: cache_hash=projection['sha256']
            need(projection['sha256']==cache_hash,'A/B fixture cache contents differ')
    pairs=[]
    for i in range(1,5):
        finals={s['row']['side']:s for s in samples if s['row']['pair']==i and s['row']['phase']=='noop'}
        a,b=(finals[s]['primary_ms'] for s in SOURCES)
        pairs.append(dict(pair=i,scenario=gate.SCENARIO,orientation=gate.ORDER[i-1],baseline_total_ms=a,
                          candidate_total_ms=b,baseline=sample_record(a),candidate=sample_record(b)))
    result=gate.evaluate_pairs(pairs)
    improvement=all(Decimal(p['delta_ms'])<0 for p in result['pairs'])
    result.update(scope='V9 fixed before/after experiment; not original #701',
                  status='regression_hold' if result['crossed'] else
                         'limited_observed_improvement' if improvement else 'benefit_unproven',
                  calls=16,preparation_mqb_calls=5,cache_sha256=cache_hash,
                  samples=[{**s,'primary_ms':str(s['primary_ms']),'outer_ms':str(s['outer_ms'])} for s in samples],
                  clears_hold=False,cause=None,performance_release_acceptance=False)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['plan','admit','check-sources','freeze','unpack','prepare-measure','audit'])
    p.add_argument('--root',type=Path);p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
    p.add_argument('--inputs',type=Path);p.add_argument('--archive',type=Path);p.add_argument('--sha256')
    p.add_argument('--output',type=Path)
    a=p.parse_args()
    try:
        result=None
        if a.command=='plan':result=protocol()
        elif a.command=='admit':
            result=admit(load(a.root/'request.json'));check_repo(a.repo)
            need(same(result,{k:os.environ.get(k) for k in KEYS}),'Saved/live request mismatch')
        elif a.command=='check-sources':check_sources(a.root)
        elif a.command=='freeze':result=freeze(a.root,a.repo)
        elif a.command=='unpack':unpack(a.archive,a.sha256,a.inputs)
        elif a.command=='prepare-measure':result=prepare_measure(a.root,a.inputs,a.repo)
        else:result=audit(a.root,a.inputs)
        if a.output:write(a.output,result)
        return 1 if a.command=='audit' and result['crossed'] else 0
    except (ValueError,OSError,KeyError,TypeError,subprocess.CalledProcessError) as exc:
        if a.output and not a.output.exists():write(a.output,dict(status='INVALID',error=str(exc),clears_hold=False,cause=None))
        print('REFUSED: '+str(exc),file=sys.stderr);return 2

if __name__=='__main__':sys.exit(main())
