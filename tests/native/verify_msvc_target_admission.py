"""Offline audit of actual target API/native process/cache evidence.

The synthetic self-test tests record acceptance, not Windows execution. Finite
cache snapshots and joined client processes do not authorize a writer lease.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import tempfile
from pathlib import Path


class EvidenceError(ValueError):
    pass


def need(condition, message):
    if not condition:
        raise EvidenceError(message)


def uint(value, name):
    need(type(value) is int and value >= 0, 'invalid unsigned field: ' + name)
    return value


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def audit(root: Path) -> dict:
    report = load(root / 'observation.json')
    need(type(report.get('schema')) is int and report['schema'] == 1, 'unknown target report schema')
    mode, config = report.get('mode'), report.get('configuration')
    need(mode in ('complete', 'cancel', 'failure-cancel') and config in ('Debug', 'Release'), 'wrong case identity')
    for field in ('gate_passed', 'overlap', 'retained_exited', 'real_processes'):
        need(report.get(field) is True, 'unproven native control: ' + field)
    for field in ('process_token_seen', 'cli_integrated', 'safe_to_transfer_write_lease', 'historical_cause_resolved'):
        need(report.get(field) is False, 'incorrect authority/token: ' + field)
    need(report.get('target_api') == 'run_with_compile_admission_stop' and report.get('observation_error') == '', 'wrong target entry/observation failure')
    children = report.get('observed_compilers')
    need(type(children) is list and len(children) == 2, 'two retained real compiler identities required')
    need(len({(uint(p.get('pid'), 'pid'), uint(p.get('created'), 'created')) for p in children}) == 2,
         'compiler identities duplicated')
    need(all(p['pid'] > 0 and p['created'] > 0 for p in children), 'invalid compiler identity')
    stopping, failing = mode != 'complete', mode == 'failure-cancel'
    counts = (2, 0, 0) if stopping else (3, 1, 1)
    for field, value in zip(('compiler_invocations', 'link_invocations', 'run_invocations'), counts):
        need(uint(report.get(field), field) == value, 'unexpected successor/dispatch count: ' + field)
    expected_reuse = [True, not failing, not stopping]
    reuse = report.get('post_reusable')
    need(type(reuse) is list and all(type(x) is bool for x in reuse) and reuse == expected_reuse,
         'failed/pending source was reusable or completed cache invalid')
    need(report.get('post_link_reusable') is (not stopping), 'target freshness falsely satisfied or completed target dirty')
    evidence = root / 'evidence'
    target = {phase: load(evidence / phase / 'target.json') for phase in ('cold', 'warm', 'subject')}
    for phase in ('cold', 'warm'):
        r = target[phase]
        need(r.get('succeeded') is True and r.get('linked') is (phase == 'cold'), 'cold/warm target path failed')
        cs = r.get('compiles')
        need(type(cs) is list and len(cs) == 3 and all(c.get('compiled') is (phase == 'cold') for c in cs), 'cold/warm compile decisions invalid')
        need([Path(c['source'].replace('\\', '/')).name for c in cs] == ['work0.cpp', 'work1.cpp', 'work2.cpp'], 'source result mapping invalid')
    subject = target['subject']
    if stopping:
        need(subject.get('succeeded') is False and type(subject.get('code')) is int
             and subject['code'] == (7 if failing else 9), 'original failure/cancellation classification wrong')
        waves = subject.get('waves')
        need(type(waves) is list and len(waves) == 1, 'unexpected/absent target wave evidence')
        w = waves[0]
        need(w.get('execution_sources') == [0, 1, 2], 'sparse execution source mapping changed')
        need(w['inspection'].get('outcome') == 'succeeded' and uint(w['inspection'].get('started_count'), 'inspection count') == 3,
             'inspection did not complete before execution')
        execution = w['execution']
        need(execution.get('outcome') == ('failed' if failing else 'cancelled'), 'phase hid original failure')
        need(uint(execution.get('worker_count'), 'workers') == 2 and uint(execution.get('started_count'), 'started') == 2,
             'wrong typed execution count')
        for field in ('stop_requested', 'admission_stop_observed', 'stopped_before_all_items'):
            need(execution.get(field) is True, 'missing stop boundary: ' + field)
        attempts = w.get('attempts')
        need(type(attempts) is list and len(attempts) == 3 and attempts[0].get('state') == 'succeeded'
             and attempts[0].get('compiled') is True and attempts[2] == {'state': 'not_started'}, 'successful sibling or pending item lost')
        need(attempts[1].get('state') == ('failed' if failing else 'succeeded'), 'wrong original item1 outcome')
        if not failing:
            need(attempts[1].get('compiled') is True, 'cancelled sibling result missing')
    else:
        need(subject.get('succeeded') is True and subject.get('linked') is True, 'complete target did not link')
        need(len(subject.get('compiles', [])) == 3 and all(c.get('compiled') is True for c in subject['compiles']), 'complete target skipped dirty work')
    result_count = 0
    for phase in ('cold', 'warm', 'subject'):
        expected = [] if phase == 'warm' else ['work0', 'work1']
        if phase == 'cold' or (phase == 'subject' and not stopping):
            expected += ['work2', 'link', 'run']
        directory = evidence / phase
        for suffix in ('result.json', 'stdout.txt', 'stderr.txt', 'argv.json'):
            actual = {p.name[:-len('.' + suffix)] for p in directory.glob('*.' + suffix)}
            need(actual == set(expected), f'{phase}: missing/unexpected {suffix}')
        for stem in expected:
            r = load(directory / (stem + '.result.json'))
            need('infrastructure_error' not in r and type(r.get('exit_code')) is int and r.get('cancelled') is False, 'native process failed/terminated or invalid result')
            out = (directory / (stem + '.stdout.txt')).read_bytes().decode('utf-8')
            err = (directory / (stem + '.stderr.txt')).read_bytes().decode('utf-8')
            args = load(directory / (stem + '.argv.json'))
            need(type(args) is list and args and all(type(a) is str for a in args), 'missing native argv')
            if stem.startswith('work'):
                need(Path(args[0].replace('\\', '/')).name.lower() == 'cl.exe', 'noncompiler substituted')
                need(all(option in args for option in ('/Zi', '/FS', '/bigobj', '/MTd' if config == 'Debug' else '/MT')),
                     'compiler policy changed')
                need(any(a.startswith('/Fd') and a.endswith('compiler.pdb') for a in args), 'PDB output not recorded')
                need(any(a.replace('\\', '/').endswith('/' + stem + '.cpp') for a in args), 'argv/source index mismatch')
            if phase == 'subject' and stem == 'work1' and failing:
                need(r['exit_code'] != 0 and 'MQB_TARGET_EXPECTED_COMPILER_FAILURE' in out + err, 'original injected compiler failure absent')
                need(subject.get('original_exit') == r['exit_code'] and subject.get('original_stdout') == out
                     and subject.get('original_stderr') == err, 'target error lost original native diagnostics')
                need(subject['waves'][0]['attempts'][1].get('exit_code') == r['exit_code'], 'per-source failure exit mismatch')
                need(Path(subject['source'].replace('\\', '/')).name == 'work1.cpp', 'error attributed to wrong source')
            else:
                need(r['exit_code'] == 0, 'required positive native tool failed')
            result_count += 1
    snapshots = {}
    keys = {'link_cache', 'executable', 'source0_cache', 'source1_cache', 'source2_cache'}
    for phase in ('cold', 'before', 'after', 'inspected'):
        record = load(evidence / 'snapshots' / (phase + '.json'))
        need(set(record) == keys, 'incomplete artifact snapshot inventory')
        for key, value in record.items():
            for field in ('size', 'mtime', 'file_id', 'volume'):
                need(uint(value.get(field), field) > 0, 'zero snapshot identity/size')
            digest = value.get('sha256')
            need(type(digest) is str and len(digest) == 64 and all(c in '0123456789abcdef' for c in digest), 'invalid SHA256')
            if key != 'executable':
                raw = (evidence / 'snapshots' / (phase + '-' + key + '.bytes')).read_bytes()
                need(len(raw) == value['size'] and hashlib.sha256(raw).hexdigest() == digest, 'snapshot bytes/hash mismatch')
        snapshots[phase] = record
    need(snapshots['cold'] == snapshots['before'], 'warm no-op rewrote artifact')
    need(snapshots['after'] == snapshots['inspected'], 'read-only inspect changed cache/artifact')
    if stopping:
        for key in ('link_cache', 'executable', 'source2_cache') + (('source1_cache',) if failing else ()):
            need(snapshots['before'][key] == snapshots['after'][key], 'stopped target published/modified ' + key)
    else:
        need(snapshots['before']['link_cache'] != snapshots['after']['link_cache'], 'positive target never published its link cache')
    return dict(accepted=True, configuration=config, mode=mode, original_results=result_count,
                target_endpoint_real_tools=True, source_cache_policy='completed TUs may retain valid caches; no project rollback',
                snapshots=snapshots, historical_cause_resolved=False, safe_to_transfer_write_lease=False)


def fixture(root, config='Debug', mode='complete'):
    stopping, failing = mode != 'complete', mode == 'failure-cancel'
    dump(root / 'observation.json', dict(schema=1, mode=mode, configuration=config, gate_passed=True,
        overlap=True, retained_exited=True, real_processes=True, process_token_seen=False, cli_integrated=False,
        safe_to_transfer_write_lease=False, historical_cause_resolved=False, target_api='run_with_compile_admission_stop',
        observation_error='', observed_compilers=[dict(pid=10, created=100), dict(pid=11, created=101)],
        compiler_invocations=2 if stopping else 3, link_invocations=0 if stopping else 1, run_invocations=0 if stopping else 1,
        post_reusable=[True, not failing, not stopping], post_link_reusable=not stopping))
    for phase in ('cold', 'warm', 'subject'):
        r = dict(succeeded=True, linked=phase != 'warm', compiles=[dict(source=f'C:/src/work{i}.cpp', compiled=phase != 'warm', warnings=0) for i in range(3)])
        if phase == 'subject' and stopping:
            r = dict(succeeded=False, code=7 if failing else 9, message='synthetic target error', source='C:/src/work1.cpp' if failing else '',
                waves=[dict(inspection=dict(outcome='succeeded', started_count=3),
                    execution=dict(outcome='failed' if failing else 'cancelled', worker_count=2, started_count=2,
                        stop_requested=True, admission_stop_observed=True, stopped_before_all_items=True),
                    execution_sources=[0,1,2], attempts=[dict(state='succeeded', compiled=True),
                        dict(state='failed', exit_code=2) if failing else dict(state='succeeded', compiled=True), dict(state='not_started')])])
            if failing:
                r.update(original_exit=2, original_stdout='MQB_TARGET_EXPECTED_COMPILER_FAILURE\n', original_stderr='')
        directory = root / 'evidence' / phase
        dump(directory / 'target.json', r)
        names = [] if phase == 'warm' else ['work0', 'work1']
        if phase == 'cold' or (phase == 'subject' and not stopping): names += ['work2','link','run']
        for name in names:
            bad = phase == 'subject' and name == 'work1' and failing
            dump(directory / (name + '.result.json'), dict(exit_code=2 if bad else 0, cancelled=False))
            (directory / (name + '.stdout.txt')).write_bytes(('MQB_TARGET_EXPECTED_COMPILER_FAILURE\n' if bad else '').encode('utf-8'))
            (directory / (name + '.stderr.txt')).write_bytes(b'')
            args = ['C:/tools/cl.exe', '/Zi', '/FS', '/bigobj', '/MTd' if config == 'Debug' else '/MT', '/FdC:/compiler.pdb', f'C:/src/{name}.cpp'] if name.startswith('work') else [f'C:/tools/{name}.exe']
            dump(directory / (name + '.argv.json'), args)
    for phase in ('cold','before','after','inspected'):
        record = {}
        for key in ('link_cache','executable','source0_cache','source1_cache','source2_cache'):
            raw=(key + '-synthetic-data').encode()
            record[key]=dict(size=len(raw), sha256=hashlib.sha256(raw).hexdigest(), mtime=10, file_id=20, volume=30)
            if key != 'executable':
                dest=root/'evidence/snapshots'/f'{phase}-{key}.bytes';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(raw)
        if not stopping and phase in ('after','inspected'): record['link_cache']['mtime'] = 20
        dump(root/'evidence/snapshots'/f'{phase}.json',record)


def self_test():
    rows=[]
    def change(root, relative, mutate):
        p=root/relative;r=load(p);mutate(r);dump(p,r)
    mutations=[
        ('stale-target-treated-fresh', lambda r: change(r,'observation.json',lambda d:d.update(post_link_reusable=True))),
        ('wrong-outcome', lambda r: change(r,'evidence/subject/target.json',lambda d:d.update(code=9))),
        ('error-lost', lambda r: change(r,'evidence/subject/target.json',lambda d:d.update(original_stdout=''))),
        ('stop-lost', lambda r: change(r,'evidence/subject/target.json',lambda d:d['waves'][0]['execution'].update(admission_stop_observed=False))),
        ('pending-counted', lambda r: change(r,'evidence/subject/target.json',lambda d:d['waves'][0]['attempts'][2].update(state='succeeded'))),
        ('source-mapping', lambda r: change(r,'evidence/subject/target.json',lambda d:d['waves'][0].update(execution_sources=[1,0,2]))),
        ('wrong-source', lambda r: change(r,'evidence/subject/target.json',lambda d:d.update(source='C:/src/work0.cpp'))),
        ('zeroed-original', lambda r: change(r,'evidence/subject/work1.result.json',lambda d:d.update(exit_code=0))),
        ('missing-diagnostic', lambda r:(r/'evidence/subject/work1.stderr.txt').unlink()),
        ('extra-result', lambda r:dump(r/'evidence/subject/work2.result.json',dict(exit_code=0,cancelled=False))),
        ('hidden-link', lambda r:dump(r/'evidence/subject/link.argv.json',['link.exe'])),
        ('missing-argv', lambda r:(r/'evidence/subject/work0.argv.json').unlink()),
        ('process-cancelled', lambda r:change(r,'evidence/subject/work1.result.json',lambda d:d.update(cancelled=True))),
        ('wrong-flag', lambda r:change(r,'evidence/subject/work1.argv.json',lambda d:d.remove('/FS'))),
        ('fake-warm-hit', lambda r:change(r,'evidence/warm/target.json',lambda d:d.update(linked=True))),
        ('bad-cache-bytes', lambda r:(r/'evidence/snapshots/after-link_cache.bytes').write_bytes(b'wrong')),
        ('changed-cache-time', lambda r:change(r,'evidence/snapshots/after.json',lambda d:d['link_cache'].update(mtime=11))),
        ('missing-cache-copy', lambda r:(r/'evidence/snapshots/before-source1_cache.bytes').unlink()),
        ('unobserved-overlap', lambda r:change(r,'observation.json',lambda d:d.update(overlap=False))),
        ('token-passed', lambda r:change(r,'observation.json',lambda d:d.update(process_token_seen=True))),
        ('failed-source-reused', lambda r:change(r,'observation.json',lambda d:d.update(post_reusable=[True,True,False]))),
        ('lease-authorized', lambda r:change(r,'observation.json',lambda d:d.update(safe_to_transfer_write_lease=True))),
        ('duplicate-handles', lambda r:change(r,'observation.json',lambda d:d.update(observed_compilers=[dict(pid=1,created=2)]*2))),
        ('string-native-exit', lambda r:change(r,'evidence/subject/work1.result.json',lambda d:d.update(exit_code='2'))),
        ('boolean-count', lambda r:change(r,'observation.json',lambda d:d.update(compiler_invocations=True))),
    ]
    with tempfile.TemporaryDirectory() as directory:
        base=Path(directory)
        tests=[(c+'-'+m,c,m,None) for c in ('Debug','Release') for m in ('complete','cancel','failure-cancel')]
        def crlf(r):
            value='MQB_TARGET_EXPECTED_COMPILER_FAILURE\r\n'
            (r/'evidence/subject/work1.stdout.txt').write_bytes(value.encode())
            change(r,'evidence/subject/target.json',lambda d:d.update(original_stdout=value))
        tests += [('raw-crlf-preserved','Debug','failure-cancel',crlf)]
        tests += [(name,'Debug','failure-cancel',mutate) for name,mutate in mutations]
        for i,(name,config,mode,mutate) in enumerate(tests):
            root=base/str(i);fixture(root,config,mode)
            if mutate: mutate(root)
            error=None
            try: audit(root)
            except (ValueError,KeyError,TypeError,OSError) as exc:error=str(exc)
            expected = mutate is None or name == 'raw-crlf-preserved'
            rows.append(dict(name=name,expected_accepted=expected,accepted=error is None,
                passed=(error is None)==expected,error=error))
    return dict(synthetic_only=True, cases=rows, passed=all(r['passed'] for r in rows))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--self-test',action='store_true');p.add_argument('--case',type=Path)
    p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    need(not a.output.exists(),'refuse to overwrite previous audit')
    try:
        result=self_test() if a.self_test else audit(a.case)
        ok=result.get('passed',result.get('accepted',False))
    except (ValueError,KeyError,TypeError,OSError) as e:
        result=dict(accepted=False,error=str(e),safe_to_transfer_write_lease=False);ok=False
    dump(a.output,result);print(json.dumps(dict(accepted=ok,output=str(a.output))));return 0 if ok else 1


if __name__=='__main__':raise SystemExit(main())
