"""Read only the fixed completed N/P001 archive; no MQB execution or ETW control."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
from zipfile import ZipFile
import noop_profile_policy as policy

SHA = '26f18bb76029aaf6efc3c0d1da4b16b7db8961f75ff86a8dc74e6bdffdb3f981'
SIZE = 19935212
COMMIT = '163d0a727549a00c526d4dad5ad9bd6a1e34efd4'
RUN = '35994887542'
READER_SHA = '980b25d780ea4408ab7659bfa1a75d0d0312e6ea26a21480f2782f186df2fde6'
need = policy.b.require

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def safe_members(infos):
    seen = set()
    for i in infos:
        p = PurePosixPath(i.filename)
        need(i.orig_filename == i.filename, 'Normalized or truncated member name')
        need(i.filename and not p.is_absolute() and '..' not in p.parts and
             '\\' not in i.filename and ':' not in i.filename and
             not i.is_dir() and not stat.S_ISLNK(i.external_attr >> 16), 'Unsafe member')
        need(i.filename not in seen, 'Duplicate member')
        seen.add(i.filename)
    return seen

def prepare(archive, work, output):
    need(archive.is_file() and not archive.is_symlink() and archive.stat().st_size == SIZE,
         'Wrong fixed archive size/type')
    need(digest(archive) == SHA, 'Wrong fixed archive hash')
    need(not work.exists() and not output.exists(), 'Fresh directories required')
    repo = Path(__file__).resolve().parents[2]
    reader = repo/'tests/native/read_noop_windows_etl.cs'
    need(policy.e.sha(policy.e.canonical(reader.read_bytes())) == READER_SHA, 'Reader changed')
    with ZipFile(archive) as z:
        need(len(safe_members(z.infolist())) == 193 and
             sum(i.file_size for i in z.infolist()) == 66449891, 'Wrong fixed inventory')
        need(z.testzip() is None, 'Bad ZIP CRC')
        work.mkdir(parents=True); z.extractall(work)
    root = work/'evidence'; request = policy.b.load(work/'request.json')
    for key, value in dict(GITHUB_REPOSITORY='Iviesever/msvc-quick-build',
            GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REF='refs/heads/main',
            GITHUB_SHA=COMMIT, GITHUB_WORKFLOW_SHA=COMMIT, REVIEWED_COMMIT=COMMIT,
            GITHUB_RUN_ID=RUN, GITHUB_RUN_NUMBER='1', GITHUB_RUN_ATTEMPT='1',
            ALLOCATION='701-noop-profile-policy-001', PROFILE_SHA256=policy.PROFILE_SHA).items():
        need(request[key] == value, 'Wrong original request')
    audit = policy.audit(root)
    need(audit == policy.b.load(root/'journal-audit.json'), 'Original journal differs')
    plan = policy.b.load(root/'plan.json')
    for name in plan['source_hashes']:
        need(policy.e.canonical((root/'source'/name).read_bytes()) ==
             policy.e.canonical((repo/name).read_bytes()), 'Frozen source changed')
    # Fixed files have fewer than100000 records each. Read the full existing file
    # to retain interval rundown; unchanged reader limits remain in force.
    traces = [dict(t, lo=1, hi=9223372036854775807) for t in audit['trace_files']]
    result = dict(capture_run=RUN, capture_commit=COMMIT, artifact_sha256=SHA,
                  reader_sha256=READER_SHA, traces=traces,
                  new_mqb_calls=0, new_etw_sessions=0, trace_health_verified=False,
                  cause=None, clears_hold=False)
    output.mkdir(parents=True)
    (output/'input.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('archive','work','output'): p.add_argument('--'+n, type=Path, required=True)
    a = p.parse_args(); prepare(a.archive, a.work, a.output)
