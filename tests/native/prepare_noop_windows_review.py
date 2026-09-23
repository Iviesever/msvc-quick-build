"""Prepare only the pinned completed windows001 artifact for offline native decoding.
No MQB, WPR, compilation, trace creation, or arbitrary-archive fallback.
"""
from __future__ import annotations
import argparse, hashlib, json, re, stat
from pathlib import Path, PurePosixPath
from zipfile import ZipFile

ARTIFACT_SHA = '95348c1dbd88f976bff3140ad11603d835aafb1669315b1ef808baf484b3abc3'
ARTIFACT_BYTES = 32366284
CAPTURE_SHA = '47ad98681e44b2387508217dba8bc525d1947314'
RUN_ID = '35872432138'
PROFILE_SHA = '7ef59ac788bfc554273d4a3ad028288e79fec428889be395765fd176c79406ec'

def require(value, message):
    if not value: raise ValueError(message)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()

def load(path): return json.loads(path.read_text(encoding='utf-8-sig'))

def safe_members(infos):
    require(len(infos) == 333 and sum(i.file_size for i in infos) == 147340594,
            'Wrong fixed archive member/size inventory')
    seen = set()
    for i in infos:
        p = PurePosixPath(i.filename)
        require(not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename and
                ':' not in i.filename and not stat.S_ISLNK(i.external_attr >> 16), 'Unsafe member')
        require(i.filename not in seen and not i.is_dir(), 'Duplicate/directory member')
        seen.add(i.filename)
    return seen

def bounds(record):
    calls = record['calls'][1:]
    require(len(calls) in (1, 2), 'Wrong measured calls')
    for c in calls:
        k = c['clock']
        require(type(k['frequency']) is int and k['frequency'] == 10000000 and
                all(type(k[t]) is int and k[t] > 0 for t in ('outer_start','native_start','native_end','outer_end')),
                'Wrong QPC type/frequency')
        require(k['outer_start'] <= k['native_start'] <= k['native_end'] <= k['outer_end'], 'Clock order')
        require(c['exit_code'] == 0 and c['error'] is None, 'Failed call')
    # Fixed 10ms context on each side; diagnostic decode only, not extra execution.
    return calls[0]['clock']['outer_start']-100000, calls[-1]['clock']['outer_end']+100000

def prepare(archive, work, output):
    require(archive.is_file() and not archive.is_symlink() and archive.stat().st_size == ARTIFACT_BYTES,
            'Wrong artifact size/type')
    require(digest(archive) == ARTIFACT_SHA, 'Wrong original artifact digest')
    require(not work.exists(), 'Fresh extraction directory required')
    output.mkdir(parents=True, exist_ok=True)
    require(not (output/'input.json').exists(), 'Existing review output')
    with ZipFile(archive) as z:
        safe_members(z.infolist()); require(z.testzip() is None, 'ZIP CRC failure')
        work.mkdir(parents=True)
        z.extractall(work)
    request = load(work/'request.json'); root = work/'evidence'; plan = load(root/'plan.json')
    require(request['GITHUB_RUN_ID'] == RUN_ID and request['GITHUB_RUN_NUMBER'] == '1' and
            request['GITHUB_RUN_ATTEMPT'] == '1' and request['GITHUB_SHA'] == CAPTURE_SHA and
            request['GITHUB_WORKFLOW_SHA'] == CAPTURE_SHA and request['REVIEWED_COMMIT'] == CAPTURE_SHA and
            request['ALLOCATION'] == '701-causal-windows-001' and request['PROFILE_SHA256'] == PROFILE_SHA,
            'Wrong capture request')
    require(plan['reviewed_commit'] == CAPTURE_SHA and len(plan['cells']) == 12, 'Wrong capture plan')
    audit = load(root/'journal-audit.json')
    require(audit['calls'] == 32 and audit['traced_calls'] == 20 and audit['windows'] == 12 and
            audit['status'] == 'window_journal_complete_trace_unreviewed' and
            audit['trace_health_verified'] is False and audit['clears_hold'] is False and audit['cause'] is None,
            'Wrong journal-only verdict')
    indexed = {x['cell']: x for x in audit['trace_files']}
    require(len(indexed) == 12, 'Trace inventory')
    traces = []
    for cell in plan['cells']:
        cid = cell['id']; require(re.fullmatch('[12]-(omit|middle_off|middle_on)-(baseline|candidate)', cid), 'Cell name')
        trace = root/'traces'/cid/'trace.etl'; expected = indexed[cid]
        require(trace.is_file() and 0 < trace.stat().st_size <= 268435456 and
                trace.stat().st_size == expected['size'] and digest(trace) == expected['sha256'], 'Trace identity')
        lo, hi = bounds(load(root/'cells'/(cid+'.json')))
        traces.append(dict(cell=cid, sha256=expected['sha256'], size=expected['size'], lo=lo, hi=hi))
    require(sum(x['size'] for x in traces) == 120586240, 'Trace total')
    result = dict(capture_commit=CAPTURE_SHA, capture_run=RUN_ID, artifact_sha256=ARTIFACT_SHA,
                  traces=traces, new_mqb_calls=0, new_etw_sessions=0,
                  status='offline_decode_inputs_verified_not_a_causal_verdict', clears_hold=False)
    with (output/'input.json').open('x', encoding='utf-8') as f: json.dump(result,f,indent=2)
    return result

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive',type=Path,required=True); p.add_argument('--work',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True); a=p.parse_args()
    prepare(a.archive,a.work,a.output)
