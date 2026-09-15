"""Two preregistered scheduler-drain cases; no product changes or retry loop.

Issue #164 comment5675603659. Raw failures and unavailable observations remain data.
The original trace/observer/span primitives are reused, not the direct-drain
high-level analyser. This experiment cannot authorize a merge or a release.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import zipfile
import verify_pdb_file_trace as trace
import verify_pdb_invocations as inv
import verify_pdb_observers as obs

BASE = '55f57a84ad938da10d0e28b4578cd1aef6d7f903'
BASE_TREE = '5f48fe99cd252148f4d6ff46f15878d7a656fb1b'
TOOL_HEAD = 'caaea69ce17ba689b80f723790b8de73909da083'
TOOL_TREE = 'df456835b443f464b2869d045905c785c5007660'
TOOL_RUN = '34965806286'
TOOL_ARTIFACT_SHA = '376fb6abcef7496a762b3ce9ebeedf76459bf5eef7f9a12952fe378f943dca11'
BUILD_JOB = 104369841207
BUNDLE_SHA = '9fabb1745891a8ddf7dbf9f69d4382d40a7a67764b5eb73dbd49bec7c985acb6'
INPUT_SHA = '71ceb8b6c861f9e2b7ba36aea680b3909cf21b65380e0853bd584fe3c3419220'
BUILDER_SHA = 'deab6c9dea5342f3a83ca79e2be3f51d483b03a8f045fbfa8411a8e6ad645c8c'
NEW_FILES = ['.github/workflows/scheduler-query-diagnostic.yml',
             'tests/native/scheduler_query.patch.json', 'tests/native/scheduler_query_diagnostic.py']
LIMITS = dict(probe_build_attempts=2, recorder_build_attempts=1, case_slots=2, capture_attempts=2)
FALSE_AUTH = dict(historical_cause_resolved=False, causal_attribution_proven=False,
                 safe_to_integrate_cancellation=False, safe_to_transfer_write_lease=False,
                 authorizes_merge=False, authorizes_release=False)
need = trace.need
load = trace.load


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write('\n')


def command(args: list[str], output: Path, cwd: Path) -> int:
    """One call, original stdout/stderr and return code. No native retry."""
    output.mkdir(parents=True, exist_ok=False)
    write(output / 'arguments.json', args)
    with (output / 'stdout.txt').open('wb') as out, (output / 'stderr.txt').open('wb') as err:
        try:
            result = subprocess.run(args, cwd=cwd, stdout=out, stderr=err, check=False)
        except OSError as exc:
            write(output / 'result.json', dict(exit_code=None, launch_error=str(exc)))
            raise
    write(output / 'result.json', dict(exit_code=result.returncode, launch_error=None))
    print(f'{output.name}: exit {result.returncode}', flush=True)
    return result.returncode


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(['git', *args], cwd=root, text=True).strip()


def source_identity(root: Path) -> dict:
    head = git(root, 'rev-parse', 'HEAD')
    need(os.environ.get('GITHUB_RUN_ATTEMPT') == '1', 'No repeated workflow attempt')
    need(os.environ.get('GITHUB_SHA') == head, 'Workflow/checkout mismatch')
    need(git(root, 'rev-parse', 'HEAD^') == TOOL_HEAD, 'Wrong correction parent')
    need(git(root, 'rev-parse', BASE + '^{tree}') == BASE_TREE, 'Wrong base tree')
    need(git(root, 'diff', '--name-status', BASE, 'HEAD').splitlines() ==
         ['A\t' + n for n in NEW_FILES], 'Only three diagnostic additions permitted')
    need(not git(root, 'status', '--porcelain', '--untracked-files=no'), 'Tracked source modified')
    need((root / 'VERSION').read_text().strip() == '5.5.0', 'VERSION changed')
    return dict(base=BASE, head=head, tree=git(root, 'rev-parse', 'HEAD^{tree}'),
                run=os.environ['GITHUB_RUN_ID'], attempt=1, host=platform.node(),
                os=platform.platform(), image=os.environ.get('ImageVersion'), limits=LIMITS)


def derive(root: Path, output: Path) -> dict:
    patch = load(root / 'tests/native/scheduler_query.patch.json')
    need(patch['base_commit'] == BASE and patch['schema'] == 1, 'Patch identity')
    raw = (root / patch['source_path']).read_bytes().replace(b'\r\n', b'\n')
    blob = hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest()
    need(blob == patch['input_blob'], 'Original probe blob changed')
    result = raw.decode()
    for old, new in patch['replacements']:
        need(result.count(old) == 1, 'Ambiguous patch anchor: ' + old)
        result = result.replace(old, new)
    need(sha(result.encode()) == patch['output_sha256'], 'Derived probe hash mismatch')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8', newline='\n') as f:
        f.write(result)
    return dict(input_blob=blob, output_sha256=patch['output_sha256'], replacements=len(patch['replacements']))


def extract_builder(bundle: Path, destination: Path) -> None:
    need(sha(bundle.read_bytes()) == BUNDLE_SHA, 'Retained bundle hash')
    with zipfile.ZipFile(bundle) as z:
        raw = z.read('original-input.zip')
    need(sha(raw) == INPUT_SHA, 'Original input hash')
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        builder = z.read('builder.exe')
    need(sha(builder) == BUILDER_SHA, 'Fixed builder hash')
    with destination.open('xb') as f:
        f.write(builder)


def build(root: Path, bundle: Path, output: Path) -> None:
    need(False, 'Build budget already consumed; reuse the pinned first-run tools')
    identity = source_identity(root)
    output.mkdir(parents=True, exist_ok=False)
    write(output / 'preflight.json', identity)
    subprocess.run(['git', 'archive', '--format=zip', '--output=' + str(output / 'source.zip'), 'HEAD'], cwd=root, check=True)
    builder = output / 'builder.exe'
    extract_builder(bundle, builder)
    generated = root / '.mqb/scheduler-query-derived/probe.cpp'
    write(output / 'derived.json', derive(root, generated))
    shutil.copyfile(generated, output / 'derived-probe.cpp')
    config = load(root / 'cpp/mqb.json')
    sources = sorted(set(config['discovery']['extra_sources']))
    actual = sorted(p.relative_to(root / 'cpp').as_posix() for p in (root / 'cpp/src').rglob('*.cpp')
                    if p.relative_to(root / 'cpp').as_posix() != 'src/app/main.cpp')
    need(sources == actual and config['build']['standard'] in ('23', 'c++23'), 'Production manifest drift')
    binaries = {'builder.exe': BUILDER_SHA}
    for mode in ('debug', 'release'):
        name = 'scheduler_query_' + mode
        args = [str(builder), str(generated)] + ['cpp/' + p for p in sources]
        args += ['--env', 'vs', '--no-discover', '--std', 'c++23', '--' + mode, '--runtime', 'MTd' if mode == 'debug' else 'MT']
        for p in config['build']['include_dirs']:
            args += ['-I', 'cpp/' + p]
        for p in config['build']['compiler_args']:
            args += ['--compiler-arg', p]
        args += ['-D', 'MQB_VERSION="ownership-probe"', '--lib', 'shell32.lib', '--lib', 'Rstrtmgr.lib', '-o', name]
        need(command(args, output / ('build-' + mode), root) == 0, 'Probe build failed; stop, no retry')
        exe = output / (name + '.exe')
        shutil.copyfile(root / '.mqb/bin' / (name + '.exe'), exe)
        binaries[exe.name] = sha(exe.read_bytes())
        need(command([str(exe), '--evidence-contract-self-test'], output / ('contract-' + mode), root) == 0, 'Native contract failed')
    name = 'scheduler_query_recorder'
    args = [str(builder), 'cpp/tests/platform/windows/pdb_file_event_probe.cpp', 'cpp/src/platform/windows/CommandLine.cpp',
            '--env', 'vs', '--no-discover', '--std', 'c++23', '--release', '--runtime', 'MT', '-I', 'cpp/include',
            '--compiler-arg', '/W4', '--compiler-arg', '/permissive-', '--lib', 'advapi32.lib', '--lib', 'tdh.lib', '-o', name]
    need(command(args, output / 'build-recorder', root) == 0, 'Recorder build failed; stop, no retry')
    exe = output / (name + '.exe')
    shutil.copyfile(root / '.mqb/bin' / exe.name, exe)
    binaries[exe.name] = sha(exe.read_bytes())
    write(output / 'identity.json', dict(identity, binaries=binaries, product_translation_units=len(sources),
                                       diagnostic_cases_executed=0, captures_executed=0))


def policy(policy: dict, observation: dict, mode: str) -> bool:
    need(mode in ('rm-on', 'rm-off'), 'Unknown arm')
    enabled = mode == 'rm-on'
    need(type(policy.get('schema')) is int and policy['schema'] == 1 and policy.get('rm_queries_enabled') is enabled and
         type(policy.get('expected_query_slots')) is int and policy['expected_query_slots'] == 4, 'Query policy mismatch')
    need(policy.get('historical_cause_resolved') is False and policy.get('safe_to_transfer_write_lease') is False, 'Query authority')
    need(policy.get('profile') == observation.get('profile') == 'modules-release' and
         policy.get('origin') == observation.get('origin') == 'A-started' and
         observation.get('ending') == 'scheduler-drain' and observation.get('endpoint_mode') == 'default' and
         observation.get('scheduler_api_used') is True, 'Not the preregistered scheduler case')
    for error, owners in obs.QUERY_FIELDS:
        need(error in observation and owners in observation, 'Missing query observation')
        if enabled:
            obs.uint(observation[error], error)
            need(type(observation[owners]) is list, 'Invalid owner set')
        else:
            need(observation[error] is None and observation[owners] is None, 'Unattempted query invented evidence')
    for flag in obs.QUERY_FLAGS:
        need(flag in observation and (type(observation[flag]) is bool if enabled else observation[flag] is None), 'Invented resource identity')
    return enabled


def case_audit(slot: Path, mode: str) -> dict:
    fixture, traces = slot / 'fixture', slot / 'trace'
    capture, decode = load(traces / 'capture.json'), load(traces / 'decode.json')
    need((traces / 'events.etl').stat().st_size == decode['etl_bytes'], 'ETL size mismatch')
    need(load(traces / 'readiness.json') == capture.get('readiness'), 'Readiness identity')
    events = [json.loads(line) for line in (traces / 'events.jsonl').read_text(encoding='utf-8-sig').splitlines()]
    native_root = load(slot / 'native-root.json')['path']
    audited = trace.audit(capture, decode, events, native_root, require_readiness=True)
    events = trace.checked_events(capture, decode, events)
    observation = load(fixture / 'observation.json')
    enabled = policy(load(fixture / 'query-policy.json'), observation, mode)
    need(all(observation.get(k) is False for k in ('safe_to_transfer_write_lease', 'safe_to_integrate_cancellation')), 'Invalid safety authority')
    envelope = load(fixture / 'default-envelope.json')
    need(all(envelope.get(k) is True for k in ('cleanup_verified', 'outer_lifecycle_verified', 'endpoint_override_absent')) and
         envelope.get('remaining_servers') == [] and type(envelope['remaining_servers']) is list, 'Cleanup unproven')
    expected = ['A/warm', 'A/provider', 'A/work0', 'B/warm', 'B/provider', 'B/work0', 'B/work1', 'B/recovery']
    if observation['B0_exit'] == observation['B1_exit'] == 0:
        expected += ['B/link']
        if observation['B_link_exit'] == 0:
            expected += ['B/run']
        else:
            need(observation['B_run_exit'] == -2, 'Failed link ran')
    else:
        need(observation['B_link_exit'] == observation['B_run_exit'] == -2, 'Failed compiler linked/ran')
    for suffix in ('argv.txt', 'invocation-begin.json', 'invocation-end.json'):
        names = {p.relative_to(fixture).as_posix().removesuffix('.' + suffix) for p in fixture.rglob('*.' + suffix)}
        need(names == set(expected), 'Missing/unexpected native invocation: ' + suffix)
    rows = []
    fields = {'B/work0': 'B0_exit', 'B/work1': 'B1_exit', 'B/link': 'B_link_exit', 'B/run': 'B_run_exit', 'B/recovery': 'recovery_compile_exit'}
    for stem in expected:
        result = load(fixture / (stem + '.result.json'))
        begin, end = (load(fixture / (stem + '.' + s + '.json')) for s in ('invocation-begin', 'invocation-end'))
        argv = (fixture / (stem + '.argv.txt')).read_text(encoding='utf-8-sig').splitlines()
        children = inv.bind_span(begin, end, result, capture, events, argv[0], stem.split('/')[-1])
        if stem in fields:
            need(result['exit_code'] == observation[fields[stem]], 'Outcome disagreement')
        if stem.split('/')[-1] not in ('link', 'run'):
            need(all(x in argv for x in ('/FS', '/Zi', '/O2', '/MT')), 'Original flags changed')
            need(('/bigobj' in argv) == (stem == 'A/work0'), 'Large A flag changed')
        outputs = [(fixture / (stem + '.' + s + '.txt')).read_text(encoding='utf-8-sig') for s in ('stdout', 'stderr')]
        rows.append(dict(stem=stem, result=result, children=children, C1041=any('C1041' in x for x in outputs)))
    drain = load(fixture / 'A/drain.json')
    scheduler = load(fixture / 'A/scheduler.json')
    need(scheduler.get('api') == 'BoundedWorkScheduler::run_with_admission_stop', 'Wrong scheduler API')
    for key in ('schema', 'worker_count', 'started_count', 'bridge_wait_error'):
        need(type(scheduler.get(key)) is int, 'Missing/typed scheduler field: ' + key)
    for key in ('scheduler_succeeded', 'stop_requested', 'admission_stop_observed',
                'stopped_before_all_items', 'event_observed', 'forwarded_during_callback'):
        need(type(scheduler.get(key)) is bool, 'Missing scheduler boundary: ' + key)
    need(scheduler.get('safe_to_transfer_write_lease') is False and
         type(scheduler.get('callback_exception')) is str, 'Scheduler authority/diagnostic')
    first = load(fixture / 'A/work0.result.json')
    need(first['exit_code'] == drain.get('first_compile_exit'), 'Scheduler/first compiler disagreement')
    need(drain.get('work_compiles_dispatched') == 1 and drain.get('pending_compile_exit') == -2 and
         drain.get('stop_observed') is True and drain.get('safe_to_transfer_write_lease') is False, 'Scheduler admission evidence failed')
    owner_begin = load(fixture / 'B/warm.invocation-begin.json')
    owner = (owner_begin['owner_pid'], owner_begin['owner_created_filetime'])
    rootkey = trace.canonical(native_root, capture).rstrip('\\')
    calls = []
    need({p.name for p in fixture.glob('observer-query-*.json')} == {f'observer-query-{i}.json' for i in range(4)}, 'Query slot inventory')
    for i, side in enumerate(('B', 'B', 'A', 'A')):
        group = load(fixture / f'observer-query-{i}.json')
        need(trace.canonical(group['path'], capture) == rootkey + '\\' + side.lower() + '\\compiler.pdb', 'Query target')
        calls += obs.query_group_calls(group, f'query-{i}', capture, events, owner, enabled)
        error, owners = obs.QUERY_FIELDS[i]
        need(group['query_error'] == observation[error] and group['owners'] == observation[owners], 'Query changed its original result')
    snapshots = []
    for phase in ('before-A', 'after-A', 'after-B'):
        snapshot = load(fixture / f'pdb-{phase}.json')
        need(snapshot.get('phase') == phase and snapshot.get('safe_to_transfer_write_lease') is False, 'Snapshot identity')
        for kind, filename in (('pdb', 'compiler.pdb'), ('pch', 'common.pch')):
            for side in ('A', 'B'):
                group = snapshot[kind][side]
                need(trace.canonical(group['path'], capture) == rootkey + '\\' + side.lower() + '\\' + filename, 'Metadata target')
                calls += obs.group_calls(group, 'metadata', f'{phase}/{side}/{kind}', capture, events, owner)
        pair = snapshot['pdb']
        known = all(pair[side]['open_error'] == 0 and pair[side]['identity_error'] == 0 for side in ('A','B'))
        if known:
            for side in ('A','B'):
                need(type(pair[side]['volume_serial']) is str and pair[side]['volume_serial'].isdigit() and
                     type(pair[side]['file_id']) is str and len(pair[side]['file_id']) == 32 and
                     all(c in '0123456789abcdef' for c in pair[side]['file_id']), 'Malformed physical identity')
            same = (pair['A']['volume_serial'],pair['A']['file_id']) == (pair['B']['volume_serial'],pair['B']['file_id'])
            need(pair.get('same_file') is same, 'Physical alias record mismatch')
        else:
            need(pair.get('same_file') is None, 'Unknown physical identity invented alias decision')
        snapshots.append(snapshot)
    ordered = sorted(calls, key=lambda r: r['before_qpc'])
    need(all(a['after_qpc'] <= b['before_qpc'] for a, b in zip(ordered, ordered[1:])), 'Overlapping coordinator calls')
    matched = obs.correlate(calls, [p for p in audited['target_open_pairs'] if p['path'].startswith(rootkey + '\\')])
    root_result = load(fixture / 'A.result.json')
    need(root_result['exit_code'] == observation['A_exit'] and root_result['cancelled'] is False, 'A outcome mismatch')
    scheduler_ok = all(scheduler[k] == v for k,v in dict(schema=1,worker_count=1,started_count=1,bridge_wait_error=0).items()) and all(
        scheduler[k] is True for k in ('scheduler_succeeded','stop_requested','admission_stop_observed',
                                       'stopped_before_all_items','event_observed','forwarded_during_callback')) and (
        scheduler['callback_exception'] == '' and scheduler.get('scheduler_error_code') is None and first['exit_code'] == 0)
    control = all(observation[k] is True for k in ('lifecycle_ok', 'drain_control_ok', 'unmanaged_control_ok'))
    need(not control or scheduler_ok, 'Claimed passing control without scheduler boundary')
    need(capture['child']['exit_code'] == (0 if control else 1), 'Original case exit does not match controls')
    return dict(FALSE_AUTH, evidence_complete=True, original_control_ok=control, query_mode=mode,
                raw_observation=observation, scheduler=scheduler, scheduler_boundary_verified=scheduler_ok, drain=drain, invocations=rows,
                metadata_snapshots=snapshots, trace_audit=audited, observer_calls=calls, correlations=matched)


def compare_inputs(left: Path, right: Path) -> dict:
    def inputs(slot):
        root = slot / 'fixture'
        native = load(slot / 'native-root.json')['path']
        values = {}
        for p in root.rglob('*'):
            if p.is_file() and (p.suffix in ('.cpp', '.hpp', '.ixx') or p.name.endswith('.argv.txt')):
                value = p.read_bytes()
                if p.name.endswith('.argv.txt'):
                    value = value.decode('utf-8-sig').replace(native, '<fixture>').encode()
                values[p.relative_to(root).as_posix()] = value
        return values
    a, b = inputs(left), inputs(right)
    conditional = {'B/link.argv.txt', 'B/run.argv.txt'}
    only = set(a) ^ set(b)
    need(only <= conditional, 'Input difference beyond unattempted link/run')
    common = sorted(set(a) & set(b))
    need(len(common) >= 10 and any(n.endswith('.ixx') for n in common), 'Input inventory incomplete')
    need(all(a[k] == b[k] for k in common), 'Common source/argv changed')
    return dict(common_inputs_verified=True, common=[dict(path=k, sha256=sha(a[k])) for k in common],
                only_left=sorted(set(a)-set(b)), only_right=sorted(set(b)-set(a)),
                conditional_unattempted_commands_not_fabricated=True, **FALSE_AUTH)



def validate_allocation(old: dict, current: dict, identity: dict, runner_name: str) -> dict:
    """Use GitHub allocations + standard hosted-Windows contract, not hostnames.

    A computer name is retained as descriptive metadata, not a globally unique
    VM identity. Native clean-endpoint/outer-cleanup checks remain mandatory.
    """
    need(type(old.get('total_count')) is int and old['total_count'] == len(old['jobs']) == 2,
         'Incomplete original allocation response')
    builds = [j for j in old['jobs'] if j.get('id') == BUILD_JOB]
    need(len(builds) == 1, 'Original build job absent/ambiguous')
    build_job = builds[0]
    need(str(build_job.get('run_id')) == TOOL_RUN and build_job.get('head_sha') == TOOL_HEAD and
         build_job.get('run_attempt') == 1 and build_job.get('status') == 'completed' and
         build_job.get('conclusion') == 'success', 'Original build allocation mismatch')
    need(type(current.get('total_count')) is int and current['total_count'] == len(current['jobs']) == 1,
         'Only one current observation job is permitted')
    job = current['jobs'][0]
    need(str(job.get('run_id')) == identity['run'] != TOOL_RUN and job.get('head_sha') == identity['head'] and
         job.get('run_attempt') == 1 and job.get('status') == 'in_progress' and
         job.get('name') == 'Observe existing tools on a fresh hosted Windows runner', 'Current allocation mismatch')
    for j in (build_job, job):
        need(type(j.get('runner_id')) is int and j['runner_id'] > 0 and
             j.get('runner_group_name') == 'GitHub Actions' and j.get('labels') == ['windows-latest'],
             'Not the required standard hosted Windows allocation')
    need(job.get('runner_name') == runner_name and type(runner_name) is str and runner_name and
         job['runner_id'] != build_job['runner_id'] and job['id'] != build_job['id'],
         'Build and observation allocations are not distinct')
    return dict(build_job=build_job, observation_job=job, distinct_allocations=True,
                isolation_basis='GitHub standard hosted Windows: fresh VM for each job',
                hostname_not_an_isolation_identifier=True, **FALSE_AUTH)


def reuse(root: Path, archive: Path, output: Path) -> None:
    source_identity(root)
    raw = archive.read_bytes()
    need(sha(raw) == TOOL_ARTIFACT_SHA, 'Original tool artifact digest mismatch')
    output.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        names = z.namelist()
        need(len(names) == len(set(names)) == 29 and z.testzip() is None, 'Original tool ZIP inventory/CRC')
        for name in names:
            need(not Path(name).is_absolute() and '..' not in Path(name).parts, 'Unsafe archive path')
        z.extractall(output)
    built = load(output / 'identity.json')
    need((built['head'],built['tree'],built['run']) == (TOOL_HEAD,TOOL_TREE,TOOL_RUN), 'Original tool provenance')
    for name, digest in built['binaries'].items():
        need(sha((output / name).read_bytes()) == digest, 'Original tool bytes changed')


def observe(root: Path, inputs: Path, output: Path) -> bool:
    output.mkdir(parents=True, exist_ok=False)
    write(output / 'startup.json', dict(host=platform.node(), runner_name=os.environ.get('RUNNER_NAME'),
          run=os.environ.get('GITHUB_RUN_ID'), head=os.environ.get('GITHUB_SHA'), job=os.environ.get('GITHUB_JOB')))
    identity = source_identity(root)
    write(output / 'controller.json', identity)
    subprocess.run(['git','archive','--format=zip','--output='+str(output/'controller-source.zip'),'HEAD'],cwd=root,check=True)
    need(os.name == 'nt' and os.environ.get('GITHUB_ACTIONS') == 'true' and
         os.environ.get('RUNNER_ENVIRONMENT') == 'github-hosted' and
         os.environ.get('MQB_OWNERSHIP_DISPOSABLE_HOST') == '1' and
         '_MSPDBSRV_ENDPOINT_' not in os.environ, 'Not a disposable default-endpoint host')
    built = load(inputs / 'identity.json')
    write(output / 'compared-identities.json', dict(built=built, observing=identity))
    need((built['head'],built['tree'],built['run']) == (TOOL_HEAD,TOOL_TREE,TOOL_RUN), 'Wrong original tools')
    need(os.environ.get('GITHUB_REPOSITORY') == 'Iviesever/msvc-quick-build' and
         os.environ.get('GITHUB_JOB') == 'observe', 'Unexpected observation workflow context')
    old, current = load(root/'allocation/original-jobs.json'), load(root/'allocation/current-jobs.json')
    write(output/'allocation-raw.json', dict(original=old,current=current))
    allocation = validate_allocation(old,current,identity,os.environ.get('RUNNER_NAME'))
    write(output/'allocation.json',allocation)
    need(set(built['binaries']) == {'builder.exe','scheduler_query_debug.exe','scheduler_query_release.exe','scheduler_query_recorder.exe'}, 'Binary inventory')
    for name, digest in built['binaries'].items():
        need(sha((inputs / name).read_bytes()) == digest, 'Binary bytes changed: ' + name)
    write(output / 'preflight.json', dict(identity, built=built, allocation=allocation))
    rows = []
    slots = []
    for index, mode in enumerate(('rm-on', 'rm-off'), 1):
        slot = output / f'{index:02d}-{mode}'
        slot.mkdir()
        fixture = slot / 'fixture'
        write(slot / 'native-root.json', dict(path=str(fixture)))
        args = [str(inputs / 'scheduler_query_recorder.exe'), str(slot / 'trace'),
                str(inputs / 'scheduler_query_release.exe'), '--scheduler-query-case', str(fixture),
                'modules-release', 'A-started', 'scheduler-drain', 'scheduler-query-' + str(index), mode]
        code = command(args, slot / 'call', root)
        row = dict(mode=mode, recorder_exit=code, evidence_complete=False, original_control_ok=None, cleanup_verified=False)
        try:
            envelope = load(fixture / 'default-envelope.json')
            row['cleanup_verified'] = all(envelope.get(k) is True for k in ('cleanup_verified', 'outer_lifecycle_verified', 'endpoint_override_absent')) and envelope.get('remaining_servers') == []
            need(code == 0, 'Recorder infrastructure failure')
            result = case_audit(slot, mode)
            write(slot / 'audit.json', result)
            row.update(evidence_complete=True, original_control_ok=result['original_control_ok'],
                       C1041_tools=[r['stem'] for r in result['invocations'] if r['C1041']])
        except (ValueError, KeyError, TypeError, OSError) as exc:
            row['error'] = str(exc)
        rows.append(row)
        slots.append(slot)
        write(output / f'progress-{index}.json', dict(cases=rows, remaining_slots=2-index, limits=LIMITS, **FALSE_AUTH))
        # A compiler failure with complete evidence is retained and DOES NOT
        # erase the second planned arm. Unknown cleanup/collection stops it.
        if not row['cleanup_verified'] or not row['evidence_complete']:
            break
    pair = None
    pair_error = None
    if len(rows) == 2 and all(r['evidence_complete'] for r in rows):
        try:
            pair = compare_inputs(*slots)
            write(output / 'input-equivalence.json', pair)
        except (ValueError, KeyError, TypeError, OSError) as exc:
            pair_error = str(exc)
    good = len(rows) == 2 and pair is not None and all(r['original_control_ok'] is True for r in rows)
    write(output / 'summary.json', dict(cases=rows, attempted_capture_slots=len(rows), not_run=2-len(rows),
                                       all_original_controls_passed=good, input_comparison_error=pair_error, **FALSE_AUTH))
    return good


def self_test(root: Path) -> dict:
    rows = []
    p = dict(schema=1, profile='modules-release', origin='A-started', rm_queries_enabled=False,
             expected_query_slots=4, historical_cause_resolved=False, safe_to_transfer_write_lease=False)
    o = dict(profile='modules-release', origin='A-started', ending='scheduler-drain', endpoint_mode='default', scheduler_api_used=True)
    for error, owners in obs.QUERY_FIELDS:
        o[error] = o[owners] = None
    for flag in obs.QUERY_FLAGS:
        o[flag] = None
    cases = [('unknowns-preserved', True, lambda p,o: None),
             ('wrong-ending', False, lambda p,o:o.update(ending='drain')),
             ('wrong-profile', False, lambda p,o:o.update(profile='modules-debug')),
             ('wrong-origin', False, lambda p,o:o.update(origin='preexisting')),
             ('wrong-endpoint', False, lambda p,o:o.update(endpoint_mode='private')),
             ('not-scheduler', False, lambda p,o:o.update(scheduler_api_used=False)),
             ('zero-error-invented', False, lambda p,o:o.update(warm_pdb_owner_error=0)),
             ('empty-owners-invented', False, lambda p,o:o.update(warm_pdb_owners=[])),
             ('resource-flag-invented', False, lambda p,o:o.update(B_pdb_service_identity_observed=False)),
             ('missing-observation', False, lambda p,o:o.pop('active_pdb_owners')),
             ('wrong-slots', False, lambda p,o:p.update(expected_query_slots=3)),
             ('boolean-slots', False, lambda p,o:p.update(expected_query_slots=True)),
             ('boolean-schema', False, lambda p,o:p.update(schema=True)),
             ('wrong-arm', False, lambda p,o:p.update(rm_queries_enabled=True)),
             ('causal-authority', False, lambda p,o:p.update(historical_cause_resolved=True)),
             ('lease-authority', False, lambda p,o:p.update(safe_to_transfer_write_lease=True))]
    for name, expected, mutate in cases:
        pp, oo = copy.deepcopy((p,o)); mutate(pp,oo)
        try:
            policy(pp,oo,'rm-off'); actual=True
        except (ValueError, KeyError, TypeError):
            actual=False
        rows.append(dict(name=name, passed=actual==expected))
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)/'derived.cpp'
        d = derive(root,out)
        need(d['replacements']==10, 'Patch inventory')
        try:
            derive(root,out)
        except FileExistsError:
            rows.append(dict(name='derive-refuses-overwrite',passed=True))
        else:
            rows.append(dict(name='derive-refuses-overwrite',passed=False))
    pp, oo = copy.deepcopy((p,o)); pp['rm_queries_enabled'] = True
    for error, owners in obs.QUERY_FIELDS:
        oo[error] = 0; oo[owners] = []
    for flag in obs.QUERY_FLAGS:
        oo[flag] = False
    for name, expected, mutate in [
        ('query-on-actual-empty-snapshot',True,lambda p,o:None),
        ('query-on-unknown-is-not-success',False,lambda p,o:o.update(warm_pdb_owners=None)),
        ('query-on-boolean-error',False,lambda p,o:o.update(warm_pdb_owner_error=True)),
        ('query-on-width-overflow',False,lambda p,o:o.update(warm_pdb_owner_error=2**32)),
        ('query-on-negative-error',False,lambda p,o:o.update(warm_pdb_owner_error=-1))]:
        ppp, ooo = copy.deepcopy((pp,oo)); mutate(ppp,ooo)
        try:
            policy(ppp,ooo,'rm-on'); actual=True
        except (ValueError,KeyError,TypeError):
            actual=False
        rows.append(dict(name=name,passed=actual==expected))
    config=load(root/'cpp/mqb.json')
    need(config['build']['standard'] in ('23','c++23'),'Unsupported source standard')
    rows.append(dict(name='actual-manifest-language-standard',passed=True))
    need(all(r['passed'] for r in rows), 'New contract test failed')
    with tempfile.TemporaryDirectory() as tmp:
        left,right = Path(tmp)/'left',Path(tmp)/'right'
        for slot in (left,right):
            fixture=slot/'fixture'; fixture.mkdir(parents=True)
            write(slot/'native-root.json',dict(path=str(fixture)))
            for i in range(10):
                (fixture/f'{i}.cpp').write_text('int f(){return 1;}')
            (fixture/'provider.ixx').write_text('export module M;')
            (fixture/'work.argv.txt').write_text(str(fixture)+' /FS /Zi /O2 /MT')
        compare_inputs(left,right)
        rows.append(dict(name='complete-paired-inputs',passed=True))
        (left/'fixture/B').mkdir(); (left/'fixture/B/link.argv.txt').write_text('link')
        result=compare_inputs(left,right)
        need(result['only_left']==['B/link.argv.txt'],'Conditional command was fabricated')
        rows.append(dict(name='unattempted-link-not-invented',passed=True))
        (right/'fixture/work.argv.txt').write_text('changed flags')
        try:
            compare_inputs(left,right)
        except ValueError:
            rows.append(dict(name='changed-flags-refused',passed=True))
        else:
            raise ValueError('Changed flags accepted')
    allocation_identity=dict(run='999',head='correction')
    bj=dict(id=BUILD_JOB,run_id=int(TOOL_RUN),head_sha=TOOL_HEAD,run_attempt=1,status='completed',conclusion='success',
            runner_id=101,runner_name='build',runner_group_name='GitHub Actions',labels=['windows-latest'])
    cj=dict(id=2,run_id=999,head_sha='correction',run_attempt=1,status='in_progress',
            name='Observe existing tools on a fresh hosted Windows runner',runner_id=102,runner_name='observe',
            runner_group_name='GitHub Actions',labels=['windows-latest'])
    for name,expected,mutate in [
        ('distinct-allocations-not-hostnames',True,lambda a,b:None),
        ('same-allocation-refused',False,lambda a,b:b.update(runner_id=101)),
        ('wrong-original-head',False,lambda a,b:a.update(head_sha='other')),
        ('failed-build-refused',False,lambda a,b:a.update(conclusion='failure')),
        ('wrong-current-head',False,lambda a,b:b.update(head_sha='other')),
        ('same-run-refused',False,lambda a,b:b.update(run_id=int(TOOL_RUN))),
        ('rerun-refused',False,lambda a,b:b.update(run_attempt=2)),
        ('self-hosted-refused',False,lambda a,b:b.update(labels=['self-hosted'])),
        ('wrong-runner-binding',False,lambda a,b:b.update(runner_name='other')),
        ('boolean-runner-id-refused',False,lambda a,b:b.update(runner_id=True)),
        ('wrong-runner-group',False,lambda a,b:b.update(runner_group_name='custom'))]:
        aa,bb=copy.deepcopy((bj,cj));mutate(aa,bb)
        try:
            validate_allocation(dict(total_count=2,jobs=[aa,dict(id=999)]),
                                dict(total_count=1,jobs=[bb]),allocation_identity,'observe');actual=True
        except (ValueError,KeyError,TypeError):actual=False
        rows.append(dict(name=name,passed=actual==expected))
    need(all(r['passed'] for r in rows),'Allocation contract test failed')
    originals = [trace.self_test(), inv.self_test(), obs.self_test()]
    need(all(r['passed'] for r in originals), 'Original pure contract tests failed')
    return dict(new_tests=rows, originals=originals, synthetic_only=True, diagnostic_cases_executed=0, **FALSE_AUTH)


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('self-test','build','reuse','observe','audit'))
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--input',type=Path)
    parser.add_argument('--arm',choices=('rm-on','rm-off'))
    args=parser.parse_args()
    root=args.root.resolve(); output=args.output.resolve()
    try:
        if args.mode=='self-test':
            write(output,self_test(root))
        elif args.mode=='build':
            need(args.input is not None,'Bundle required'); build(root,args.input.resolve(),output)
        elif args.mode=='reuse':
            need(args.input is not None,'Original tool archive required'); reuse(root,args.input.resolve(),output)
        elif args.mode=='observe':
            need(args.input is not None,'Input binaries required')
            return 0 if observe(root,args.input.resolve(),output) else 1
        else:
            need(args.input is not None and args.arm is not None,'Slot/arm required')
            write(output,case_audit(args.input.resolve(),args.arm))
        return 0
    except Exception as exc:
        print(type(exc).__name__ + ': ' + str(exc),file=sys.stderr)
        return 1


if __name__=='__main__':
    raise SystemExit(main())
