"""Fixed C/D project-location contrast, #164 comment5710706245.

Reuse the retained recorder/EXEs. Equal path suffixes, jobs=auto, original
metadata-only seals. No product edits, content preread after primes, or retries.
"""
from __future__ import annotations
import argparse
import ctypes as C
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import statistics
import struct
import sys
import tempfile
import traceback
import zipfile

BASE = '55f57a84ad938da10d0e28b4578cd1aef6d7f903'
BRANCH = 'codex/private129-location-once-20260917'
PRIOR_HASH = '413a0371435301af9333972212f1a8a4cb3135d6b1b58fe3b7dc70edac7691ee'
CONTROLLER_HASH = '8dc5732864f81d80d451a747f02448a6138dfa09dd436b3b17483f98e2a39f43'
ARCHIVE_HASH = 'd70a0478c8b060e1da1b64e1361ec9ee69978277eed3d2c671149cff372c4530'
ALLOWED = {'.github/workflows/private129-location-contrast.yml', 'tests/native/private129_location_contrast.py'}
SIDES = ('baseline', 'candidate')
VOLUMES = ('C', 'D')


def require(value, message):
    if not value:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def allocated(env):
    expected = {'GITHUB_REPOSITORY': 'Iviesever/msvc-quick-build', 'GITHUB_REF': 'refs/heads/' + BRANCH,
                'GITHUB_RUN_NUMBER': '1', 'GITHUB_RUN_ATTEMPT': '1', 'GITHUB_ACTIONS': 'true',
                'RUNNER_ENVIRONMENT': 'github-hosted'}
    return all(env.get(k) == v for k, v in expected.items())


def plan():
    rows = []
    def add(phase, volume, side, block=None):
        rows.append(dict(phase=phase, volume=volume, side=side, jobs='auto', block=block))
    for phase in ('prime', 'pre'):
        for volume in VOLUMES:
            for side in SIDES:
                add(phase, volume, side)
    for block in range(1, 13):
        volumes = VOLUMES if block % 2 else VOLUMES[::-1]
        sides = SIDES if (block - 1) % 4 in (0, 3) else SIDES[::-1]
        for volume in volumes:
            for side in sides:
                add('score', volume, side, block)
    for volume in VOLUMES:
        for side in SIDES:
            add('post', volume, side)
    return rows


def peer_path(path):
    p = PureWindowsPath(path)
    require(p.is_absolute() and p.drive.upper() == 'C:', 'system temporary directory must be on C')
    other = 'D' + str(p)[1:]
    require(len(str(p)) == len(other) and str(p)[1:] == other[1:], 'unequal path suffix')
    return other


def parse_extents(raw):
    require(len(raw) >= 8, 'truncated VOLUME_DISK_EXTENTS')
    count = struct.unpack_from('<I', raw)[0]
    require(0 < count <= 128 and len(raw) == 8 + 24 * count, 'invalid extent count/size')
    result = []
    for offset in range(8, len(raw), 24):
        disk, start, length = struct.unpack_from('<I4xqq', raw, offset)
        require(start >= 0 and length > 0, 'invalid disk interval')
        result.append(dict(disk_number=disk, starting_offset=start, extent_length=length))
    return result


def volume_identity(path):
    k = C.WinDLL('kernel32', use_last_error=True)
    u32, handle = C.c_uint32, C.c_void_p
    k.GetVolumePathNameW.argtypes = [C.c_wchar_p, C.c_wchar_p, u32]
    k.GetVolumePathNameW.restype = C.c_int
    k.GetVolumeNameForVolumeMountPointW.argtypes = [C.c_wchar_p, C.c_wchar_p, u32]
    k.GetVolumeNameForVolumeMountPointW.restype = C.c_int
    k.GetVolumeInformationW.argtypes = [C.c_wchar_p, C.c_wchar_p, u32, C.POINTER(u32),
                                      C.POINTER(u32), C.POINTER(u32), C.c_wchar_p, u32]
    k.GetVolumeInformationW.restype = C.c_int
    mount, guid, label, fs = (C.create_unicode_buffer(1024) for _ in range(4))
    serial, component, flags = u32(), u32(), u32()
    result = dict(path=str(path), errors={})
    calls = [('mount', k.GetVolumePathNameW, (str(path), mount, 1024)),
             ('guid', k.GetVolumeNameForVolumeMountPointW, (mount, guid, 1024)),
             ('information', k.GetVolumeInformationW, (mount, label, 1024, C.byref(serial),
                C.byref(component), C.byref(flags), fs, 1024))]
    for name, func, args in calls:
        C.set_last_error(0)
        if not func(*args):
            result['errors'][name] = C.get_last_error()
            return result
    result.update(mount=mount.value, guid=guid.value, filesystem=fs.value, label=label.value,
                  serial=serial.value, max_component=component.value, flags=flags.value)
    k.CreateFileW.argtypes = [C.c_wchar_p, u32, u32, handle, u32, u32, handle]
    k.CreateFileW.restype = handle
    k.DeviceIoControl.argtypes = [handle, u32, handle, u32, handle, u32, C.POINTER(u32), handle]
    k.DeviceIoControl.restype = C.c_int
    k.CloseHandle.argtypes = [handle]
    k.CloseHandle.restype = C.c_int
    # Query-only access. Do not change a volume, privilege, cache or driver policy.
    h = k.CreateFileW(guid.value.rstrip('\\'), 0, 7, None, 3, 0, None)
    if h == C.c_void_p(-1).value:
        result['disk_query_error'] = C.get_last_error()
        result['disk_extents'] = None
        return result
    try:
        buf, returned = C.create_string_buffer(4096), u32()
        C.set_last_error(0)
        ok = k.DeviceIoControl(h, 0x00560000, None, 0, buf, len(buf), C.byref(returned), None)
        result['disk_query_error'] = None if ok else C.get_last_error()
        require(returned.value <= len(buf), 'disk output size overflow')
        raw = buf.raw[:returned.value]
        result.update(disk_buffer_hex=raw.hex(), disk_returned_bytes=returned.value, disk_extents=None)
        if ok:
            result['disk_extents'] = parse_extents(raw)
    finally:
        result['close_error'] = None if k.CloseHandle(h) else C.get_last_error()
    return result


def load_original(input_path, output):
    raw = input_path.read_bytes()
    require(digest(raw) == PRIOR_HASH, 'wrong prior diagnostic artifact')
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        require(z.testzip() is None, 'prior artifact CRC error')
        source = z.read('evidence/controller-source.zip')
        cumulative = z.read('original-cumulative.zip')
    require(digest(cumulative) == ARCHIVE_HASH, 'wrong original cumulative bytes')
    with zipfile.ZipFile(io.BytesIO(source)) as z:
        code = z.read('tests/native/private129_jobs_contrast.py').replace(b'\r\n', b'\n')
    require(digest(code) == CONTROLLER_HASH, 'wrong retained recorder source')
    p = output / 'retained_controller.py'
    p.write_bytes(code)
    spec = importlib.util.spec_from_file_location('retained_private129_controller', p)
    q = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = q
    spec.loader.exec_module(q)
    q.self_test()
    archive = output / 'original-cumulative.zip'
    archive.write_bytes(cumulative)
    h, helper_hash = q.extract_pinned(archive, output / 'original')
    h.self_test()
    dump(output / 'pinned-inputs.json', dict(prior_sha256=PRIOR_HASH, controller_sha256=CONTROLLER_HASH,
                cumulative_sha256=ARCHIVE_HASH, helper_sha256=helper_hash, binaries=q.EXE_HASHES))
    return q, h


def private_fixture(h, parent):
    case = h.fixture(parent, 'private', 129)
    for source in sorted(case.root.glob('*.cpp')):
        source.with_suffix('.hpp').write_bytes((case.root / 'common.hpp').read_bytes())
        source.write_text(source.read_text(encoding='utf-8').replace('common.hpp', source.with_suffix('.hpp').name), encoding='utf-8')
    require(len(list(case.root.glob('*.cpp'))) == 129, 'wrong TU count')
    return case


def summary(h, rows):
    groups, deltas = {}, {}
    for volume in VOLUMES:
        pairs = []
        for block in range(1, 13):
            p = {r['side']: r for r in rows if r['phase'] == 'score' and r['volume'] == volume and r['block'] == block}
            require(set(p) == set(SIDES), 'incomplete pairs')
            pairs.append(dict(block=block, **p))
        groups[volume] = h.summarize(pairs)
        deltas[volume] = groups[volume]['paired_deltas_ms']
    interaction = [c - d for c, d in zip(deltas['C'], deltas['D'])]
    return dict(by_volume=groups, C_minus_D_block_interaction_ms=interaction,
                interaction_median_ms=statistics.median(interaction),
                historical_flag_cleared=False, new_acceptance_threshold=None, causal_attribution_proven=False)


def self_test():
    rows = plan()
    require(len(rows) == 60, 'budget')
    require([sum(r['phase'] == p for r in rows) for p in ('prime','pre','score','post')] == [4,4,48,4], 'phase budget')
    for volume in VOLUMES:
        orders = []
        for block in range(1, 13):
            blockrows = [r for r in rows if r['phase'] == 'score' and r['block'] == block]
            vr = [r for r in blockrows if r['volume'] == volume]
            require(len(vr) == 2, 'pair missing')
            orders.append((tuple(r['side'] for r in vr), blockrows[0]['volume'] == volume))
        for sides in (SIDES, SIDES[::-1]):
            for first in (True, False):
                require(orders.count((sides, first)) == 3, 'unbalanced order')
    valid = dict(GITHUB_REPOSITORY='Iviesever/msvc-quick-build', GITHUB_REF='refs/heads/'+BRANCH,
                 GITHUB_RUN_NUMBER='1', GITHUB_RUN_ATTEMPT='1', GITHUB_ACTIONS='true', RUNNER_ENVIRONMENT='github-hosted')
    require(allocated(valid), 'valid allocation')
    for key in valid:
        for bad in ('', '2', '01', 'false'):
            require(not allocated({**valid, key: bad}), 'bad allocation')
    require(peer_path(r'C:\Users\RUNNER~1\AppData\Local\Temp\mqb-cumulative-12345678') == r'D:\Users\RUNNER~1\AppData\Local\Temp\mqb-cumulative-12345678', 'path changed')
    good = struct.pack('<I4xI4xqq', 1, 7, 4096, 8192)
    require(parse_extents(good) == [dict(disk_number=7, starting_offset=4096, extent_length=8192)], 'extent decode')
    for invalid in (b'', good[:-1], good+b'\0', bytes(32), struct.pack('<I4xI4xqq',1,7,-1,8192)):
        try:
            parse_extents(invalid)
        except RuntimeError:
            pass
        else:
            raise RuntimeError('invalid extent accepted')
    return dict(passed=True, calls=60, Windows_execution=False)


def execute(args):
    require(os.name == 'nt' and allocated(os.environ), 'not the unique authorized hosted run')
    out = args.output.resolve()
    require(not out.exists(), 'never overwrite/resume evidence')
    out.mkdir(parents=True)
    try:
        q, h = load_original(args.input.resolve(), out)
        head = q.git('rev-parse','HEAD')
        require(head == os.environ['GITHUB_SHA'] and q.git('rev-parse','HEAD^') == BASE, 'wrong controller parent')
        require(set(q.git('diff','--name-only',BASE,head).splitlines()) == ALLOWED, 'unexpected source changes')
        require(q.git('status','--porcelain','--untracked-files=no') == '', 'tracked source dirty')
        require(Path('VERSION').read_text().strip() == '5.5.0', 'VERSION changed')
        q.subprocess.run(['git','archive','-o',str(out/'controller-source.zip'),head], check=True)
        dump(out/'identity.json', dict(head=head, base=BASE, tree=q.git('rev-parse','HEAD^{tree}'),
             preregistration=5710706245, run_id=os.environ['GITHUB_RUN_ID'], run_number=1, attempt=1,
             image=os.environ.get('ImageVersion'), os=q.platform.platform(), python=sys.version))
        dump(out/'environment.json', {k:os.environ.get(k) for k in ('TEMP','TMP','PATH','INCLUDE','LIB','LIBPATH','CL','_CL_','NUMBER_OF_PROCESSORS','PROCESSOR_IDENTIFIER')})
        native = q.NativeMetrics()
        temp = Path(tempfile.gettempdir())
        peer_path(str(temp))
        require(Path('D:/').is_dir(), 'D drive missing')
        cp = Path(tempfile.mkdtemp(prefix='mqb-cumulative-', dir=temp)).resolve()
        require(re.fullmatch(r'mqb-cumulative-[a-z0-9_]{8}',cp.name) is not None, 'temporary naming drift')
        dp = Path(peer_path(str(cp)))
        require(not dp.exists(), 'peer experiment path already exists')
        dp.mkdir(parents=True)
        require(dp.resolve() == dp, 'peer canonical path differs')
        cases = {v:private_fixture(h,p) for v,p in zip(VOLUMES,(cp,dp))}
        identities = {}
        for v, case in cases.items():
            for ancestor in (case.root,*case.root.parents):
                require(not os.lstat(ancestor).st_file_attributes & 0x400, 'reparse path refused')
            identities[v] = volume_identity(case.root)
        dump(out/'volumes.json', identities)
        require(all(not r['errors'] and r.get('guid') and r.get('close_error') is None for r in identities.values()), 'missing volume identity/close failure')
        require(identities['C']['guid'] != identities['D']['guid'], 'not two distinct volumes')
        require(str(cases['C'].root)[1:] == str(cases['D'].root)[1:], 'project path suffixes differ')
        dump(out/'paths.json', {v:dict(root=str(c.root),length=len(str(c.root)),argv=c.arguments) for v,c in cases.items()})
        dump(out/'plan.json', plan())
        # Reuse the tested recorder unchanged. Only the fixed plan and budget are adapted.
        q.plan, q.MAX_CALLS = plan, 60
        rec = q.Recorder(out,h,cases['C'],native)
        before = None
        for item in plan():
            rec.case = cases[item['volume']]
            row = rec.run(item,out/'original'/item['side']/'mqb.exe')
            if item['phase'] == 'prime':
                text = h.normalized((out/row['stdout_file']).read_bytes())
                expected = 129 if item['side'] == 'baseline' else 0
                require(len(re.findall(rb'^\[compile\] ',text,re.M)) == expected, 'unexpected prime compile count')
                require(len(re.findall(rb'^\[link\] ',text,re.M)) == int(expected>0), 'unexpected prime link count')
            if item['phase'] in ('pre','post') and item['side'] == 'candidate':
                require(h.semantic_counters(rec.rows[-2]) == h.semantic_counters(row), 'A/B audit mismatch')
            if len(rec.rows) == 8:
                before = {v:h.state(c) for v,c in cases.items()}
                dump(out/'metadata-before.json',before)
            if len(rec.rows) == 56:
                after = {v:h.state(c) for v,c in cases.items()}
                dump(out/'metadata-after-scores.json',after)
                require(before == after, 'score phase mutated project state')
        after = {v:h.state(c) for v,c in cases.items()}
        dump(out/'metadata-after-audits.json',after)
        require(before == after, 'audit phase mutated project state')
        # All measurement is finished before the first full-content inventory/copy.
        dump(out/'summary.json',summary(h,rec.rows))
        dump(out/'actual-tools.json',q.actual_tools(rec.rows,out))
        contents = {v:q.inventory(c.root) for v,c in cases.items()}
        dump(out/'post-content-hashes.json',contents)
        inputs = {v:{r['path']:r['sha256'] for r in rs if r['path'].endswith(('.cpp','.hpp'))} for v,rs in contents.items()}
        require(inputs['C'] == inputs['D'] and len(inputs['C']) == 259, 'source contents differ')
        for v,case in cases.items():
            shutil.copytree(case.root,out/'fixtures'/v)
            require({r['path']:r['sha256'] for r in q.inventory(out/'fixtures'/v)} == {r['path']:r['sha256'] for r in contents[v]}, 'copied evidence mismatch')
        for side,sha in q.EXE_HASHES.items():
            require(digest((out/'original'/side/'mqb.exe').read_bytes()) == sha, 'measured EXE changed')
        require(digest(args.input.read_bytes()) == PRIOR_HASH, 'input artifact changed')
        dump(out/'completion.json',dict(completed=True,mqb_calls=len(rec.rows),prime_calls=4,audit_calls=8,
             off_calls=48,new_MQB_builds=0,new_ETW=0,fixture_executions=0,historical_flag_cleared=False,release_authorized=False))
    except Exception:
        (out/'failure.txt').write_text(traceback.format_exc(),encoding='utf-8')
        raise


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test',action='store_true')
    p.add_argument('--input',type=Path)
    p.add_argument('--output',type=Path)
    args = p.parse_args()
    result = self_test()
    if args.self_test:
        print(json.dumps(result,indent=2))
        return
    require(args.input is not None and args.output is not None,'missing input/output')
    execute(args)


if __name__ == '__main__':
    main()
