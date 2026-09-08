"""Fixed #160 decision: 40 timings-off rebuild pairs, and real save API cost.
Never rerun to select a favorable outcome; all observations remain in raw files.
"""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent / 'tests/native'))
import compare_reporting as h

BASE_SHA = '4bab036f8ca8b2982acaa8969572329ceba5579a'
HEAD_SHA = 'bf70ed1024ab37ced2393e73edc4ed8e3c4868bb'

def main():
    baseline, candidate, probe_a, probe_b, output = map(lambda p: Path(p).resolve(), sys.argv[1:])
    h.require(os.name == 'nt' and all(p.is_file() for p in [baseline, candidate, probe_a, probe_b]), 'Windows and exact binaries required')
    h.require(not output.exists(), 'Do not overwrite previous evidence')
    recorder = h.Recorder(output)
    report = {'base_sha': BASE_SHA, 'head_sha': HEAD_SHA,
              'binaries': {name: {'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for name,p in [('base',baseline),('head',candidate),('probe_base',probe_a),('probe_head',probe_b)]},
              'policy': '40 fixed AB/BA rebuild pairs, timings OFF; practical adverse flag: paired median exceeds both 5 ms and 3%, with >=30/40 slower. Not proof of no regression. Probe save API includes encode + filesystem replacement, not pure serialization. No counter=0 substitution.',
              'image_version': os.environ.get('ImageVersion'), 'scenarios': []}
    def persist():
        (output/'decision.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    persist()
    with tempfile.TemporaryDirectory(prefix='mqb-archive-decision-') as tmp:
        case = h.fixture(Path(tmp), 'static129', 129, static=True)
        recorder.run(baseline, case, 'prime')
        unit = case.root/'unit_000.cpp'
        original = unit.read_text(encoding='utf-8')
        pairs=[]
        for i in range(40):
            pair={'pair':i+1, 'orientation':'AB' if i%2==0 else 'BA'}
            sides=[('baseline',baseline),('candidate',candidate)]
            for side,binary in sides if i%2==0 else sides[::-1]:
                stamp=unit.stat().st_mtime_ns
                # Same content and length for both sides in this pair; a real
                # later write changes freshness. No future/backdated inputs.
                unit.write_text(original + f'\n// pair {i:04d}\n',encoding='utf-8')
                h.require(unit.stat().st_mtime_ns>stamp,'Real timestamp must advance')
                row=recorder.run(binary,case,f'rebuild-{i+1}-{side}')
                text=recorder.human(row,'stdout')
                h.require(text.count(b'[compile] ')==1 and b'[compile] unit_000.cpp [' in text and text.count(b'[archive] ')==1,'Expected exactly one TU and real archive')
                h.require(case.output.is_file(),'Missing library')
                before=h.state(case)
                audit=recorder.run(binary,case,f'audit-{i+1}-{side}',timings=True)
                h.assert_warm(audit,case,candidate=True)
                h.require(before==h.state(case),'Post-rebuild no-op mutated state')
                (output/f'state-{i+1}-{side}.json').write_text(json.dumps(before),encoding='utf-8')
                row['warm_audit']=audit
                pair[side]=row
            h.require(h.semantic_counters(pair['baseline']['warm_audit'])==h.semantic_counters(pair['candidate']['warm_audit']),'Audited freshness/counters differ')
            for channel in ['stdout','stderr']:
                h.require(pair['baseline'][f'human_{channel}_sha256']==pair['candidate'][f'human_{channel}_sha256'],'Rebuild transcript changed')
            pairs.append(pair)
            report['scenarios']=[{'name':'static129-single-tu-off','summary':h.summarize(pairs),'pairs':pairs}]
            persist()
        s=h.summarize(pairs)
        report['practical_adverse_flag']=(s['paired_median_delta_ms']>5 and s['paired_median_delta_pct']>3 and s['faster_pairs']<=10)
        # Separate process-level probes isolate the public cache save API from
        # compiler/librarian variability. Caller records are created beforehand.
        probe_pairs=[]
        for i in range(24):
            pair={'pair':i+1, 'orientation':'AB' if i%2==0 else 'BA'}
            sides=[('baseline',probe_a),('candidate',probe_b)]
            for side,exe in sides if i%2==0 else sides[::-1]:
                done=subprocess.run([str(exe),str(case.root/'probe.archivecache')],capture_output=True,timeout=60,check=True)
                (output/f'probe-{i+1}-{side}.stdout').write_bytes(done.stdout)
                (output/f'probe-{i+1}-{side}.stderr').write_bytes(done.stderr)
                result=json.loads(done.stdout)
                pair[side]={'external_ms':result['save_batch_ms'],**result}
            h.require(pair['baseline']['bytes']==pair['candidate']['bytes'],'Serialized size differs')
            probe_pairs.append(pair)
        report['scenarios'].append({'name':'archive-save-api-32-calls','summary':h.summarize(probe_pairs),'pairs':probe_pairs})
        persist()
    (output/'passed.txt').write_text('40 rebuild and 24 real save API pairs completed with contracts.\n')
    print(json.dumps([{k:v for k,v in s.items() if k!='pairs'} for s in report['scenarios']],indent=2))
    print('practical_adverse_flag:',report['practical_adverse_flag'])

if __name__=='__main__': main()
