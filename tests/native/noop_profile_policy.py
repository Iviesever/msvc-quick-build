"""Pinned O-only A/B x N/P policy study. Preparation/audit only; never executes.
N has no study-owned ETW. P samples only the final call. Neither journals nor
sample summaries prove performance/causality. Old consumed studies are untouched.
"""
from __future__ import annotations
import argparse
import io
from pathlib import Path
import re
import sys
from zipfile import ZipFile
import xml.etree.ElementTree as ET
import noop_causal_windows as w

c, b, e = w.c, w.b, w.e
PROFILE = 'tests/native/noop_profile_policy.wprp'
PROFILE_SHA = 'f130951e600b6079c298aee44a6fdeea010b1fd8ef76ec354cd80beb644ad072'
WINDOW_SHA = 'dd73f3b6e563207c8bbe0967f01455c22b5df739bc9ebdc9f0cca31a01dc96b9'
SOURCES = w.SOURCES + (PROFILE, 'tests/native/noop_profile_policy.py',
    'tests/native/trace_noop_profile_policy.ps1', '.github/workflows/noop-profile-policy-study.yml')
PINS = {c.PROFILE: w.PROFILE_SHA, 'tests/native/trace_noop_causal.ps1': w.CONTROL_SOURCE_SHA,
        'tests/native/collect_external_noop_boundary.ps1': c.COLLECTOR_SHA,
        'tests/native/trace_noop_causal_windows.ps1': WINDOW_SHA, PROFILE: PROFILE_SHA}


def cells():
    result = []
    for block in (1, 2):
        for policy in ('N', 'P') if block == 1 else ('P', 'N'):
            for side in ('baseline', 'candidate') if block == 1 else ('candidate', 'baseline'):
                rows = [dict(sequence=2*len(result)+i+1, phase=phase, argv=list(b.ARGV))
                        for i, phase in enumerate(('prime', 'final'))]
                result.append(dict(id=f'{block}-{policy}-{side}', block=block,
                                   policy=policy, side=side, rows=rows))
    return result


def specification():
    return dict(schema=1, purpose='original_701_O_only_NP_policy', cells=cells(),
        max_calls=16, traced_calls=4, previous_recorded_calls=104, proposed_cumulative_ceiling=120,
        limits=dict(w.LIMITS, max_windows=4, max_wpr_commands=20,
                    max_interval_queries=2, max_all_wpr_invocations=22),
        execution_allocated=False, trace_health_verified=False, cause=None, clears_hold=False)


def profile_check(path):
    raw = e.file_bytes(path)
    b.require(e.sha(e.canonical(raw)) == PROFILE_SHA, 'unreviewed policy profile bytes')
    ps = ET.fromstring(raw).find('Profiles')
    keys = ps.findall('SystemProvider/Keywords/Keyword')
    expected = c.KEYWORDS | {'SampledProfile', 'DPC', 'Interrupt'}
    b.require(len(keys) == len(expected) and {k.get('Value') for k in keys} == expected
              and all(k.get('Strict') == 'true' for k in keys), 'policy keywords changed')
    b.require({s.get('Value') for s in ps.findall('SystemProvider/Stacks/Stack')} ==
              {'CSwitch', 'ReadyThread', 'SampledProfile'}, 'policy stacks changed')
    b.require(ps.find('SystemCollector/MaximumFileSize').attrib ==
              dict(Value='256', FileMode='Sequential'), 'sequential limit changed')
    b.require(ps.find('Profile').get('Name') == 'MqbNoopPolicy', 'profile name changed')


def prepare(archive, root, repo, reviewed_commit, profile_sha):
    b.require(re.fullmatch('[0-9a-f]{40}', reviewed_commit or '') is not None, 'bad reviewed commit')
    b.require(profile_sha == PROFILE_SHA, 'unreviewed profile')
    profile_check(repo/PROFILE)
    snapshots = {name: e.file_bytes(repo/name) for name in SOURCES}
    for name, digest in PINS.items():
        b.require(e.sha(e.canonical(snapshots[name])) == digest, 'reused source changed: '+name)
    identity = e.verify_archive(archive)  # Exact original #701 including original A/B/source.
    raw = e.file_bytes(archive, 128*1024*1024)
    b.require(not root.exists(), 'fresh output required; no resume')
    root.mkdir()
    for name in ('inputs', 'source', 'cells', 'primes', 'fixtures', 'traces'):
        (root/name).mkdir()
    (root/'inputs/original-701.zip').write_bytes(raw)
    with ZipFile(io.BytesIO(raw)) as z:
        for side in b.BINARIES:
            (root/'inputs'/f'{side}.exe').write_bytes(z.read(side+'/mqb.exe'))
    for name, data in snapshots.items():
        dest = root/'source'/name; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(data)
    plan = dict(**specification(), reviewed_commit=reviewed_commit, root=str(root.absolute()),
                profile_sha256=profile_sha, original=identity,
                source_hashes={n: b.digest(v) for n,v in snapshots.items()})
    for cell in plan['cells']:
        fixture = root/'fixtures'/cell['id']; fixture.mkdir()
        for name, data in b.SOURCES.items(): (fixture/name).write_bytes(data)
    b.write_new(root/'plan.json', plan)
    return plan


def control_args(base, cell):
    segment = base+'/traces/'+cell['id']; label = 'MQB_NOOP_POLICY|'+cell['id']
    return [
        ['-start', base+'/source/'+PROFILE+'!MqbNoopPolicy.Verbose', '-filemode', '-recordtempto', segment+'/temp'],
        ['-marker', label+'|begin'], ['-marker', label+'|end'], ['-status', 'collectors', '-details'],
        ['-stop', segment+'/trace.etl', 'MQB N/P policy; not a score', '-skipPdbGen']]


def check_controls(root, plan, cell, window, first):
    instance = window['instance']
    b.require(isinstance(instance, str) and re.fullmatch('MQB-NoopPolicy-[0-9a-f]{32}', instance), 'bad instance')
    b.require(b.same(window['first_control'], first) and b.same(window['last_control'], first+4), 'control order')
    for offset, argv in enumerate(control_args(plan['root'], cell)):
        stem = f'wpr-{first+offset:02}'
        started, done = (b.load(root/(stem+s)) for s in ('.started.json', '.json'))
        actual = started['argv']; paths = {1,4} if offset == 0 else {1} if offset == 4 else set()
        b.require(isinstance(actual, list) and len(actual) == len(argv), 'control argv shape')
        b.require(all(w.same_path(a,v) if i in paths else a == v for i,(a,v) in enumerate(zip(actual,argv))), 'control target')
        b.require(b.same(started, dict(argv=actual, instance=instance)), 'control start journal')
        b.require(done['instance'] == instance and b.same(done['argv'], actual) and b.same(done['exit_code'],0)
                  and done['command_error'] is None and done['journal_errors'] == [], 'control failed')
    ticks = [window[k] for k in ('start_qpc','ready_qpc','calls_end_qpc','stop_qpc','stopped_qpc')]
    b.require(all(type(t) is int and t > 0 for t in ticks) and ticks == sorted(ticks), 'window clock')
    b.require(window['stop_attempted'] is True and window['stop_error'] is None and
              window['owned_after_stop'] is False, 'not released')
    return ticks


def check_budget(value):
    for key in ('observed_trace_bytes','segment_etl_bytes','free_bytes'):
        b.require(type(value[key]) is int and value[key] >= 0, 'invalid byte checkpoint')
    b.require(value['observed_trace_bytes'] <= w.LIMITS['total_trace_bytes'] and
              value['segment_etl_bytes'] <= w.LIMITS['segment_etl_bytes'] and
              value['free_bytes'] >= w.LIMITS['minimum_free_bytes'], 'byte checkpoint failed')


def audit(root):
    plan = b.load(root/'plan.json')
    for key,value in specification().items(): b.require(b.same(plan[key],value), 'plan changed: '+key)
    b.require(plan['profile_sha256'] == PROFILE_SHA, 'profile digest')
    b.require(set(plan['source_hashes']) == set(SOURCES), 'source inventory')
    for name,digest in plan['source_hashes'].items():
        raw = e.file_bytes(root/'source'/name)
        b.require(b.digest(raw) == digest, 'source changed: '+name)
        if name in PINS: b.require(e.sha(e.canonical(raw)) == PINS[name], 'pinned source changed')
    b.require(e.verify_archive(root/'inputs/original-701.zip') == plan['original'], 'original changed')
    for side,digest in b.BINARIES.items():
        b.require(b.digest(e.file_bytes(root/'inputs'/f'{side}.exe')) == digest, 'binary changed')
    ids = {x['id'] for x in cells()}; pids = {x['id'] for x in cells() if x['policy'] == 'P'}
    for folder in ('cells','primes'):
        b.require({p.name for p in (root/folder).iterdir()} == {x+'.json' for x in ids}, 'cell/prime inventory')
    b.require({p.name for p in (root/'traces').iterdir()} == pids, 'N must not have a trace; P must have one')
    expected = {f'wpr-{i:02}{suffix}' for i in range(1,21) for suffix in ('.started.json','.json')}
    b.require({p.name for p in root.glob('wpr-*')} == expected, 'control journal inventory')
    b.require(b.same(b.load(root/'completion.json'), dict(status='policy_calls_complete_trace_unreviewed',
        attempted=16, error=None, trace_stop_error=None, clears_hold=False, cause=None)), 'stopped completion')
    host = b.load(root/'host.json'); freq = host['qpc_frequency']
    b.require(type(freq) is int and freq > 0 and host['reviewed_commit'] == plan['reviewed_commit'], 'host/source clock')
    b.require({x.name for x in root.glob('profile-interval-*')} ==
              {f'profile-interval-{phase}{suffix}' for phase in ('before','after') for suffix in ('.started.json','.json')},
              'interval query inventory')
    queries = []
    for phase in ('before','after'):
        b.require(b.same(b.load(root/('profile-interval-'+phase+'.started.json')), dict(phase=phase,argv=['-profint'])), 'interval start journal')
        q = b.load(root/('profile-interval-'+phase+'.json'))
        b.require(q['argv'] == ['-profint'] and b.same(q['exit_code'],0) and q['error'] is None
                  and q['changes_interval'] is False and q['phase'] == phase, 'interval query failed')
        b.require(type(q['start_qpc']) is int and type(q['end_qpc']) is int and
                  0 < q['start_qpc'] <= q['end_qpc'], 'query clock')
        b.require(isinstance(q['lines'],list) and all(isinstance(x,str) for x in q['lines']) and q['lines'], 'query output missing')
        queries.append(q)
    previous = queries[0]['end_qpc']; control = 1; instances = set(); traces = []
    for cell in cells():
        rec = b.load(root/'cells'/(cell['id']+'.json'))
        b.require(b.same(rec['cell'],cell) and rec['error'] is None and len(rec['calls']) == 2, 'stopped cell')
        b.require(b.same(b.load(root/'primes'/(cell['id']+'.json')), dict(cell=cell,before=rec['before'],
                  after_prime=rec['after_prime'],call=rec['calls'][0])), 'prime checkpoint')
        before, after = b.manifest(rec['before']), b.manifest(rec['after_final'])
        expected_sources = {name: dict(size=len(data),sha256=b.digest(data)) for name,data in b.SOURCES.items()}
        b.require(set(before) == set(b.SOURCES) and all(before[n]['size'] == x['size'] and before[n]['sha256'] == x['sha256']
                  for n,x in expected_sources.items()), 'fresh sources changed')
        b.require(after == b.manifest(rec['after_prime']) and '.mqb/bin/timing_bench.exe' in after, 'fixture changed')
        check_budget(rec['budget_before']); check_budget(rec['budget_after'])
        call_ticks = []
        for row,call in zip(cell['rows'],rec['calls']):
            ticks = c.check_record(cell,row,call)
            b.require(call['clock']['frequency'] == freq and ticks[0] > previous, 'call clock/order')
            b.require(all(type(call[k]) is int and call[k] > 0 for k in ('host_tid_before','host_tid_after')), 'thread identity')
            previous = ticks[-1]; call_ticks.append(ticks)
        win = rec['window']
        if cell['policy'] == 'N':
            b.require(win is None and rec['budget_after']['segment_etl_bytes'] == 0, 'N recorded a study window')
        else:
            start,ready,end,stop,stopped = check_controls(root,plan,cell,win,control); control += 5
            b.require(win['instance'] not in instances, 'reused instance'); instances.add(win['instance'])
            b.require(call_ticks[0][-1] < start and ready <= call_ticks[1][0] <= call_ticks[1][-1] <= end, 'prime/final trace boundary')
            previous = stopped
            trace = root/'traces'/cell['id']/'trace.etl'
            b.require(trace.is_file() and 0 < trace.stat().st_size <= w.LIMITS['segment_etl_bytes'], 'trace missing/limit')
            b.require(rec['budget_after']['segment_etl_bytes'] == trace.stat().st_size, 'recorded segment size')
            traces.append(dict(cell=cell['id'],size=trace.stat().st_size,sha256=w.file_hash(trace)))
    b.require(previous < queries[1]['start_qpc'], 'post-query overlaps study')
    return dict(status='policy_journal_complete_trace_unreviewed', calls=16, traced_calls=4, windows=4,
        interval_queries=2, trace_files=traces, observed_trace_bytes=w.trace_bytes(root/'traces'),
        trace_health_verified=False, cause=None, clears_hold=False,
        limitation='Whole N/P observation policies, not a pure sampling tax. Separate native sample/schema/loss review required.')


def assess_samples(summary):
    """Fail-closed shape/absence guard on a separately reviewed numerical summary.
    This does not decode ETL or authenticate that summary, and never certifies health.
    A nonzero count permits description only, not precision, causation or HOLD release.
    """
    b.require(isinstance(summary,dict) and b.same(summary.get('schema'),1), 'sample summary schema')
    windows = summary.get('windows')
    b.require(isinstance(windows,list) and all(isinstance(x,dict) for x in windows) and [x.get('cell') for x in windows] ==
              [x['id'] for x in cells() if x['policy'] == 'P'], 'sample window inventory/order')
    reasons = []; counts = []
    for win in windows:
        cid = win['cell']
        for k in ('events_lost','buffers_lost'):
            v = win.get(k)
            if v is None: reasons.append(cid+': missing '+k)
            else:
                b.require(type(v) is int and v >= 0, 'invalid loss count')
                if v: reasons.append(cid+': '+k)
        for k in ('schema_supported','timebase_verified','interval_verified','process_identity_verified','thread_coverage_verified'):
            if win.get(k) is not True: reasons.append(cid+': '+k+' unavailable')
        threads = win.get('threads')
        if not isinstance(threads,list) or not threads:
            reasons.append(cid+': no per-thread sample counts'); continue
        seen = set()
        for t in threads:
            b.require(isinstance(t,dict), 'invalid thread row')
            for k in ('tid','process_create_qpc','samples','stack_matched_samples'):
                b.require(type(t.get(k)) is int and t[k] >= (1 if k in ('tid','process_create_qpc') else 0), 'invalid sample/thread count')
            key = (t['process_create_qpc'],t['tid']); b.require(key not in seen,'duplicate thread'); seen.add(key)
            b.require(t['stack_matched_samples'] <= t['samples'],'more stacks than samples')
            counts.append(dict(cell=cid,**{k:t[k] for k in ('tid','process_create_qpc','samples','stack_matched_samples')}))
            if t['samples'] == 0: reasons.append(cid+': zero samples for tid '+str(t['tid']))
            elif t['stack_matched_samples'] != t['samples']: reasons.append(cid+': unmatched sample stacks')
    return dict(status='inconclusive' if reasons else 'descriptive_samples_only', reasons=reasons,
                per_thread=counts, trace_health_verified=False, cause=None, clears_hold=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=('plan','prepare','audit','assess-samples'))
    p.add_argument('--output',type=Path,required=True)
    for k in ('root','repo','archive','summary'): p.add_argument('--'+k,type=Path)
    p.add_argument('--reviewed-commit'); p.add_argument('--profile-sha'); a=p.parse_args()
    try:
        if a.command == 'plan': result=specification()
        elif a.command == 'prepare':
            b.require(all((a.archive,a.root,a.repo,a.reviewed_commit,a.profile_sha)),'missing prepare args')
            result=prepare(a.archive,a.root,a.repo,a.reviewed_commit,a.profile_sha)
        elif a.command == 'audit':
            b.require(a.root is not None,'missing root'); result=audit(a.root)
        else:
            b.require(a.summary is not None,'missing summary'); result=assess_samples(b.load(a.summary))
        b.write_new(a.output,result); return 0
    except (ValueError,OSError,KeyError,TypeError) as exc:
        print('REFUSED: '+str(exc),file=sys.stderr); return 2

if __name__ == '__main__': sys.exit(main())
