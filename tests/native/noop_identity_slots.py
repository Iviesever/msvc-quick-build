"""Fixed #819 identity/slot diagnostic: prepare and audit only, never execute MQB.

A complete journal is NOT performance acceptance. The failed qualification stays
HOLD. This contract-only slice provides no native execution entry or allocation.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import ntpath
from pathlib import Path, PurePosixPath
import re
import stat
import sys
from zipfile import ZipFile

import external_noop_boundary as b

ARCHIVE_SHA = '34295afd020d1ff140b053630347f9b39004f9de1b743387864bdc9f211ffcd8'
ARCHIVE_BYTES = 10237432
IMAGES = {'baseline': '48fc85fe1777b142599e24673928bc719dc77ef635a19e757f033945337b0bea',
          'candidate': '55aff284fcd1cf09a7446c689754a40b3ad51a249f64fe709709e53ec5033af0'}
REVISIONS = {'baseline': '4cd17c74aeda33eb6a7220fa227c58fabb8b202a',
             'candidate': 'c66ff000ab278570e937bac687d97f664c33c8d9'}
LEGACY = 'tests/native/collect_external_noop_boundary.ps1'
LEGACY_SHA = '4b99ee4b1ad4de3608d3079175ff47bbd1de74e8f80dad715ac6b303698f9ebf'
SOURCE_FILES = ('tests/native/noop_identity_slots.py', 'tests/native/noop_identity_slots_runtime.psm1',
                'tests/native/external_noop_boundary.py')
LIMITS = dict(root_calls=48, returned_seconds=30, free_bytes=4294967296,
              evidence_bytes=268435456, job_minutes=20)
need = b.require
same = b.same


def plan():
    cases = [('AA', ('baseline', 'baseline')), ('AB', ('baseline', 'candidate')),
             ('BB', ('candidate', 'candidate')), ('BA', ('candidate', 'baseline'))]
    blocks, rows = [], []
    for turn in range(2):
        for label, images in (cases if turn == 0 else reversed(cases)):
            ident = f'r{turn+1}-{label}'
            order = list('LRRL' if turn == 0 else 'RLLR')
            mapping = dict(zip(('L', 'R'), images))
            block = dict(id=ident, mapping=mapping, self_control=label in ('AA', 'BB'), rows=[])
            for phase, slots in [('prime', ['L', 'R']), ('measured', order)]:
                for position, slot in enumerate(slots):
                    row = dict(sequence=len(rows)+1, block=ident, phase=phase, position=position,
                               slot=slot, image=mapping[slot], sha256=IMAGES[mapping[slot]], argv=b.ARGV.copy())
                    rows.append(row); block['rows'].append(row)
            blocks.append(block)
    return dict(schema=1, purpose='fixed_819_identity_slot_order_diagnostic', blocks=blocks,
                limits=LIMITS.copy(), original_sha256=ARCHIVE_SHA, images=IMAGES.copy(),
                sources={n: v.decode('ascii') for n, v in b.SOURCES.items()},
                prime_calls=16, measured_calls=32, execution_allocated=False, native_entry_available=False,
                original_qualification=dict(run=36682255779, attempt=1, decision='HOLD'),
                may_clear_hold=False, gate_replacement=False, etw_sessions=0)


def canonical(data):
    return data.replace(b'\r\n', b'\n')


def digest(path):
    need(path.is_file() and not path.is_symlink(), 'ordinary input file required')
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check_archive(path):
    need(path.is_file() and not path.is_symlink() and path.stat().st_size == ARCHIVE_BYTES,
         'wrong original ZIP size/type')
    data = path.read_bytes(); need(b.digest(data) == ARCHIVE_SHA, 'wrong original ZIP SHA256')
    with ZipFile(io.BytesIO(data)) as z:
        names = z.namelist()
        need(len(names) == 2553 and len(set(names)) == len(names), 'wrong ZIP inventory')
        for item in z.infolist():
            p = PurePosixPath(item.filename)
            need(not p.is_absolute() and '..' not in p.parts and '\\' not in item.filename and
                 not stat.S_ISLNK(item.external_attr >> 16) and not item.is_dir(), 'unsafe ZIP member')
        need(z.testzip() is None, 'original CRC failure')
        load = lambda name: json.loads(z.read(name).decode('utf-8-sig'), object_pairs_hook=b.unique)
        before, after = load('provenance/identity-before.json'), load('provenance/identity-after.json')
        need(before['run_id'] == '36682255779' and before['attempt'] == '1', 'wrong original run')
        need(load('noop-acceptance.json')['decision'] == 'HOLD', 'original HOLD changed')
        binaries = {}
        for side, sha in IMAGES.items():
            binary = z.read(side+'/mqb.exe')
            need(len(binary) == 1933824 and b.digest(binary) == sha == after[side] ==
                 before['inputs'][side]['binary_sha256'], 'original binary identity mismatch')
            need(before['inputs'][side]['sha'] == REVISIONS[side], 'wrong original source commit')
            source = z.read('provenance/'+side+'-source.zip')
            need(b.digest(source) == before['inputs'][side]['source_zip_sha256'], 'wrong source archive')
            binaries[side] = binary
    return data, binaries


def validate_review_claim(reviewed_commit, allocation):
    # Syntax only: matching claims do not prove approval or allocate execution.
    need(isinstance(reviewed_commit, str) and re.fullmatch('[0-9a-f]{40}', reviewed_commit) is not None,
         'invalid reviewed commit')
    need(isinstance(allocation, str) and re.fullmatch('pr232-slot-[0-9]{3}', allocation) is not None,
         'invalid separate allocation label')


def prepare(archive, root, repo, reviewed_commit, allocation):
    validate_review_claim(reviewed_commit, allocation)
    need(not root.exists() and not root.is_symlink(), 'new root required; no resume')
    for parent in (root.parent, *root.parents):
        need(not parent.is_symlink(), 'root ancestor alias refused')
    data, images = check_archive(archive)
    source = {name: (repo/name).read_bytes() for name in SOURCE_FILES}
    need(b.digest(canonical((repo/LEGACY).read_bytes())) == LEGACY_SHA, 'legacy reference changed')
    # Preserve the actual absolute spelling, not resolve() aliases behind the recorder.
    value = dict(**plan(), root=str(root.absolute()), reviewed_commit=reviewed_commit,
                 allocation_label=allocation, source_hashes={n: b.digest(v) for n, v in source.items()})
    root.mkdir()
    for name in ('inputs', 'source', 'slots', 'retired', 'blocks', 'fixtures'):
        (root/name).mkdir()
    (root/'inputs/original-819.zip').write_bytes(data)
    for side, binary in images.items():
        (root/'inputs'/(side+'.exe')).write_bytes(binary)
    for name, raw in source.items():
        target = root/'source'/name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(raw)
    b.write_new(root/'plan.json', value)
    return value


def check_call(row, value):
    need(same(value['row'], row) and value['argv'] == b.ARGV and
         value['executable_sha256'] == row['sha256'], 'wrong call identity/argv')
    need(value['dispatch_requested'] is True and value['may_clear_hold'] is False, 'wrong dispatch/proof')
    need(value['error'] is None and type(value['exit_code']) is int and value['exit_code'] == 0,
         'call failed or exit unknown')
    need(value['root_times'] is None and value['cleanup'] is None and
         value['output_format'] == 'powershell_merged_lines', 'invented OS time/capture mode')
    q = value['clock']; frequency = q['frequency']
    ticks = [q[k] for k in ('outer_start', 'native_start', 'native_end', 'outer_end')]
    need(type(frequency) is int and frequency > 0 and all(type(x) is int and x >= 0 for x in ticks)
         and ticks == sorted(ticks), 'invalid monotonic clock')
    need(ticks[-1]-ticks[0] <= LIMITS['returned_seconds']*frequency, 'returned-call time limit')
    lines = value['output_lines']
    need(isinstance(lines, list) and all(isinstance(x, str) for x in lines), 'invalid output lines')
    need(not any(x.startswith('{"type":"mqb.timings') for x in lines), 'internal timings unexpectedly enabled')
    compiles = sum(x.startswith('[compile] ') for x in lines)
    links = sum(x.startswith('[link] ') for x in lines)
    if row['phase'] == 'prime' and row['position'] == 0:
        need(compiles == 2 and links == 1, 'first prime not a fresh two-source build')
    else:
        need(compiles == links == 0 and [x for x in lines if x.startswith('[up-to-date] ')] ==
             ['[up-to-date] 2 translation units', '[up-to-date] timing_bench.exe'], 'not the fixed no-op')
    return dict(sequence=row['sequence'], block=row['block'], phase=row['phase'], slot=row['slot'],
                image=row['image'], outer_ms=(ticks[-1]-ticks[0])*1000/frequency,
                native_envelope_ms=(ticks[2]-ticks[1])*1000/frequency, root_lifetime_ms=None,
                root_cpu_ms=None, child_process_count=None)


def metadata(value, expected_sha, expected_size):
    need(value['sha256'] == expected_sha, 'slot identity mismatch')
    need(all(type(value[k]) is int and value[k] >= 0 for k in ('size', 'mtime_ticks', 'creation_ticks')),
         'invalid slot metadata')
    need(value['size'] == expected_size, 'slot size differs from verified input bytes')


def limits(value):
    need(type(value['free_bytes']) is int and value['free_bytes'] >= LIMITS['free_bytes'], 'space checkpoint failed')
    need(type(value['evidence_bytes']) is int and 0 <= value['evidence_bytes'] <= LIMITS['evidence_bytes'],
         'evidence checkpoint failed')


def audit(root):
    p = b.load(root/'plan.json')
    validate_review_claim(p['reviewed_commit'], p['allocation_label'])
    for key, expected in plan().items():
        need(same(p[key], expected), 'fixed plan changed: '+key)
    need(set(p['source_hashes']) == set(SOURCE_FILES), 'wrong collector inventory')
    for name, sha in p['source_hashes'].items():
        need(digest(root/'source'/name) == sha, 'collector source changed')
    check_archive(root/'inputs/original-819.zip')
    image_sizes = {}
    for side, sha in IMAGES.items():
        path = root/'inputs'/(side+'.exe')
        need(digest(path) == sha, 'input binary changed')
        image_sizes[side] = path.stat().st_size
    host = b.load(root/'host.json')
    need(host['plan_sha256'] == digest(root/'plan.json') and host['reviewed_commit'] == p['reviewed_commit'] and
         host['allocation_label'] == p['allocation_label'], 'wrong host request')
    finish = b.load(root/'completion.json')
    need(same(finish, dict(status='completed_diagnostic_unreviewed', attempted=48, error=None,
                          close_errors=[], may_clear_hold=False)), 'incomplete/stopped diagnostic')
    expected_names = {x['id']+suffix for x in p['blocks'] for suffix in
                      ('.started.json', '.window.json', '.result.json')}
    need({x.name for x in (root/'blocks').iterdir()} == expected_names, 'missing/extra block journals')
    need({x.name for x in (root/'fixtures').iterdir()} == {x['id'] for x in p['blocks']}, 'fixture inventory changed')
    rows, previous, previous_end = [], None, -1
    for index, block in enumerate(p['blocks']):
        stem = root/'blocks'/block['id']
        intent = b.load(Path(str(stem)+'.started.json'))
        need(same(intent, dict(block=block, attempted_before=index*6, intent_only=True)), 'bad block intent')
        window = b.load(Path(str(stem)+'.window.json'))
        record = b.load(Path(str(stem)+'.result.json'))
        need(same(record['block'], block) and record['error'] is None and record['pending'] is None and
             record['close_errors'] == [] and len(record['calls']) == 6, 'block not complete')
        need(same(window, dict(rows=block['rows'][2:], intent_only=True, after_prime=record['after_prime'],
                              slots=record['slots_before'], primes=record['calls'][:2])), 'window intent changed')
        limits(record['budget_before']); limits(record['budget_after'])
        need(set(b.manifest(record['before'])) == set(b.SOURCES), 'fixture not initially fresh')
        after = b.manifest(record['after_prime'])
        need(after == b.manifest(record['after_final']) and '.mqb/bin/timing_bench.exe' in after,
             'measured window changed fixture')
        # Physical copy hashes independently agree with post-window manifest; mtime is Windows-journal evidence.
        fixture = root/'fixtures'/block['id']
        physical = {f.relative_to(fixture).as_posix(): f for f in fixture.rglob('*') if f.is_file()}
        need(set(physical) == set(after), 'retained fixture differs')
        for name, f in physical.items():
            need(f.stat().st_size == after[name]['size'] and digest(f) == after[name]['sha256'], 'retained bytes differ')
        for slot in ('L', 'R'):
            image = block['mapping'][slot]
            metadata(record['slots_before'][slot], IMAGES[image], image_sizes[image])
            need(same(record['slots_before'][slot], record['slots_after'][slot]), 'slot changed in window')
            if previous is None:
                need(record['retired'][slot] is None, 'foreign initial slot')
            else:
                need(same(record['retired'][slot], previous['slots_after'][slot]), 'old owned identity changed')
                need(digest(root/'retired'/p['blocks'][index-1]['id']/(slot+'.exe')) ==
                     previous['slots_after'][slot]['sha256'], 'retired slot not retained')
        for row, call in zip(block['rows'], record['calls']):
            out = check_call(row, call)
            q = call['clock']; need(q['outer_start'] >= previous_end and q['frequency'] == host['qpc_frequency'], 'call clock/order')
            previous_end = q['outer_end']
            expected = p['root']+'/slots/'+row['slot']+'.exe'
            need(ntpath.normpath(call['executable']) == ntpath.normpath(expected) and
                 ntpath.normpath(call['cwd']) == ntpath.normpath(p['root']+'/fixtures/'+block['id']), 'wrong call path')
            rows.append(out)
        previous = record
    need({x.name for x in (root/'retired').iterdir()} == {x['id'] for x in p['blocks'][:-1]}, 'extra retired slots')
    for block in p['blocks'][:-1]:
        need({x.name for x in (root/'retired'/block['id']).iterdir()} == {'L.exe', 'R.exe'}, 'retired inventory')
    need({x.name for x in (root/'slots').iterdir()} == {'L.exe', 'R.exe'}, 'live slot inventory')
    for slot in ('L', 'R'):
        need(digest(root/'slots'/(slot+'.exe')) == previous['slots_after'][slot]['sha256'], 'final slot changed')
    contrasts = []
    for block in p['blocks']:
        measured = [r for r in rows if r['block'] == block['id'] and r['phase'] == 'measured']
        # Keep both adjacent pairs and signs; no pooled winner, adjusted gate or significance claim.
        for i in (0, 2):
            left, right = sorted(measured[i:i+2], key=lambda r: r['slot'])
            contrasts.append(dict(block=block['id'], self_control=block['self_control'],
                sequences=[r['sequence'] for r in measured[i:i+2]], left_image=left['image'],
                right_image=right['image'], right_minus_left_ms=right['outer_ms']-left['outer_ms']))
    return dict(status='complete_diagnostic_uninterpreted', calls=rows, contrasts=contrasts,
                original_decision='HOLD', may_clear_hold=False, gate_replacement=False,
                limitation='Envelope timings only. No root lifetime/CPU/I-O census. No subtraction of self-controls or new qualification score.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('plan', 'prepare', 'audit'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--archive', type=Path); parser.add_argument('--root', type=Path)
    parser.add_argument('--repo', type=Path); parser.add_argument('--reviewed-commit'); parser.add_argument('--allocation')
    a = parser.parse_args()
    try:
        need(not a.output.exists(), 'new output required')
        if a.command == 'plan': value = plan()
        elif a.command == 'prepare':
            need(all((a.archive, a.root, a.repo, a.reviewed_commit, a.allocation)), 'missing preparation arguments')
            value = prepare(a.archive, a.root, a.repo, a.reviewed_commit, a.allocation)
        else:
            need(a.root is not None, 'missing evidence root'); value = audit(a.root)
        b.write_new(a.output, value)
        return 0
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print('REFUSED: '+str(exc), file=sys.stderr); return 2


if __name__ == '__main__': sys.exit(main())
