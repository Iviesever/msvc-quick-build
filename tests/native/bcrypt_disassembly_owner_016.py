"""One 016 GNU reservation after selection admission, with bounded output."""
import hashlib
import os
import resource
import signal
import subprocess
import time

from bcrypt_disassembly_model_016 import LIMITS, Refusal, encode_json, require, validate_manifest
from bcrypt_selection_016 import select_target, validate_selection_evidence, validate_incoming_starts
from bcrypt_disassembly_listing_014 import parse_listing
from bcrypt_disassembly_io_014 import (
    OutputDirectory as BaseOutputDirectory, verify_environment, verify_identity,
)

DIAGNOSTIC = b'OBSERVATION_REFUSED\n'


class OutputDirectory(BaseOutputDirectory):
    @staticmethod
    def _caps():
        return {'consumed.json': LIMITS['marker_bytes'], 'target.bin': LIMITS['target_bytes'],
                'tool-stdout.bin': LIMITS['child_file_bytes'],
                'tool-stderr.bin': LIMITS['child_file_bytes'],
                'report.json': LIMITS['report_bytes']}


def child_limits():
    """Fresh 016 limits; only its reviewed Popen preexec callback invokes this."""
    for kind, key in ((resource.RLIMIT_FSIZE, 'child_file_bytes'),
                      (resource.RLIMIT_AS, 'child_address_space_bytes'),
                      (resource.RLIMIT_CPU, 'child_cpu_seconds'),
                      (resource.RLIMIT_CORE, 'child_core_bytes'),
                      (resource.RLIMIT_NOFILE, 'child_open_files')):
        pair = (LIMITS[key], LIMITS[key])
        resource.setrlimit(kind, pair)
        require(resource.getrlimit(kind) == pair, 'CHILD_LIMIT')
    for name in ('SIGPIPE', 'SIGXFZ', 'SIGXFSZ'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), signal.SIG_DFL)


def _digest(raw):
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def _reason(error):
    code = error.code if isinstance(error, Refusal) else 'IO_OR_INTERNAL_ERROR'
    return code if (type(code) is str and 1 <= len(code) <= 48 and
                    all(c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789' for c in code)) else 'IO_OR_INTERNAL_ERROR'


def _reap(process, report):
    if process is None or report['child_reaped']:
        return
    rc = process.poll()
    if rc is not None:
        report.update(child_returncode=rc, child_reaped=True)
        return
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except OSError:
        pass
    try:
        report['child_returncode'] = process.wait(timeout=1)
        report['child_reaped'] = True
    except (OSError, subprocess.TimeoutExpired):
        pass


def observe(manifest, manifest_raw, manifest_path, expected_sha, start_ns):
    out, process, report = None, None, None
    expected = {}
    deadline = start_ns + LIMITS['owner_seconds'] * 1_000_000_000

    def clock():
        now = time.monotonic_ns()
        require(type(start_ns) is int and 0 <= start_ns <= now <= deadline, 'DEADLINE')
        return now

    def identities(keep=False):
        clock()
        verify_identity({'path': manifest_path, 'bytes': len(manifest_raw),
                         'sha256': expected_sha}, LIMITS['manifest_bytes'])
        verify_environment(manifest)
        for group, cap in (('sources', LIMITS['source_bytes']),
                           ('provenance', LIMITS['provenance_bytes'])):
            for record in manifest[group].values():
                clock()
                verify_identity(record, cap)
        selection = {}
        for role in ('selection', 'selection_review'):
            clock()
            selection[role] = verify_identity(manifest['identities'][role],
                                               LIMITS[role + '_bytes'], keep=True)
        validate_selection_evidence(manifest, selection['selection'], selection['selection_review'])
        retained = {}
        for role, record in manifest['identities'].items():
            if role in selection:
                continue
            clock()
            needed = keep and role in ('image', 'index', 'admission', 'caller')
            raw = verify_identity(record, LIMITS.get(role + '_bytes', LIMITS['identity_bytes']), keep=needed)
            if needed:
                retained[role] = raw
        clock()
        return retained

    def capture():
        if not report['child_reaped']:
            return
        for name, key in (('tool-stdout.bin', 'raw_stdout'), ('tool-stderr.bin', 'raw_stderr')):
            raw = out.read(name, LIMITS['child_file_bytes'])
            expected[name] = raw
            report[key] = _digest(raw)
        report['capture_stable'] = True

    def persist():
        report['elapsed_ns_before_persistence'] = max(0, time.monotonic_ns() - start_ns)
        encode_json(dict(report, instructions=None), LIMITS['report_wrapper_bytes'])
        raw = encode_json(report, LIMITS['report_bytes'])
        out.create('report.json', raw, LIMITS['report_bytes'])
        expected['report.json'] = raw
        if report['capture_stable'] or process is None:
            out.durable_readback(expected)

    try:
        clock()
        validate_manifest(manifest)
        require(manifest['phase'] == 'actual', 'OWNER_PHASE')
        require(type(manifest_raw) is bytes and
                encode_json(manifest, LIMITS['manifest_bytes']) == manifest_raw and
                hashlib.sha256(manifest_raw).hexdigest() == expected_sha, 'MANIFEST_IDENTITY')
        out = OutputDirectory(manifest['output_dir'])
        report = {
            'schema': 'bcrypt-disassembly-report-016-v1', 'status': 'refused',
            'reason': 'NOT_COMPLETE', 'stage': 'reserved', 'consumed': True,
            'phase': 'actual', 'mode': manifest['mode'], 'call_id': manifest['call_id'],
            'source_commit': manifest['source_commit'], 'manifest_sha256': expected_sha,
            'target': manifest['target'], 'child_launch_reservations': 0,
            'child_handle_obtained': False, 'child_pid': None,
            'child_reaped': False, 'child_returncode': None, 'capture_stable': False,
            'identities_before_verified': False, 'identities_after_verified': False,
            'raw_stdout': None, 'raw_stderr': None, 'instructions': None,
            'incoming_instruction_starts_verified': False, 'verified_incoming_offsets': None,
            'completion_requires_clean_owner_exit': True,
            'native_execution_performed': False, 'outgoing_targets_followed': False,
        }
        marker = encode_json({
            'schema': 'bcrypt-disassembly-consumed-016-v1', 'consumed': True,
            'call_id': manifest['call_id'], 'mode': manifest['mode'], 'phase': 'actual',
            'source_commit': manifest['source_commit'], 'manifest_sha256': expected_sha,
            'attempt_reuse_allowed': False,
        }, LIMITS['marker_bytes'])
        expected['consumed.json'] = marker
        out.create('consumed.json', marker, LIMITS['marker_bytes'])
        report['stage'] = 'identities_before'
        inputs = identities(keep=True)
        target = select_target(manifest, inputs['image'], inputs['index'], inputs['admission'], inputs['caller'])
        del inputs
        require(0 < len(target) <= LIMITS['target_bytes'], 'TARGET_SIZE')
        report['identities_before_verified'] = True
        expected['target.bin'] = target
        out.create('target.bin', target, LIMITS['target_bytes'])
        stdout_fd = out.open_capture('tool-stdout.bin')
        expected['tool-stdout.bin'] = b''
        stderr_fd = out.open_capture('tool-stderr.bin')
        expected['tool-stderr.bin'] = b''
        out.durable_readback(expected)
        clock()
        require(signal.getsignal(signal.SIGCHLD) == signal.SIG_DFL, 'CHILD_SIGNAL_POLICY')
        report['stage'] = 'tool'
        report['child_launch_reservations'] = 1
        process = subprocess.Popen(manifest['tool_argv'], cwd=manifest['output_dir'],
                                   env=manifest['environment'], stdin=subprocess.DEVNULL,
                                   stdout=stdout_fd, stderr=stderr_fd, close_fds=True,
                                   start_new_session=True, restore_signals=True, preexec_fn=child_limits)
        report['child_handle_obtained'], report['child_pid'] = True, process.pid
        remaining = max(0.001, (deadline - time.monotonic_ns()) / 1_000_000_000)
        report['child_returncode'] = process.wait(timeout=remaining)
        report['child_reaped'] = True
        capture()
        clock()
        report['stage'] = 'identities_after'
        identities()
        report['identities_after_verified'] = True
        require(all(report[key]['bytes'] < LIMITS['child_file_bytes']
                    for key in ('raw_stdout', 'raw_stderr')), 'RAW_CAPTURE_CAP')
        require(report['child_returncode'] == 0, 'TOOL_EXIT')
        require(report['raw_stderr']['bytes'] == 0, 'TOOL_STDERR')
        report['stage'] = 'listing'
        instructions = parse_listing(expected['tool-stdout.bin'], target, manifest['target'])
        require(0 < len(instructions) <= LIMITS['instructions'], 'INSTRUCTION_COUNT')
        report['verified_incoming_offsets'] = validate_incoming_starts(instructions, manifest['target'])
        report['incoming_instruction_starts_verified'] = True
        report['instructions'] = instructions
        clock()
        report.update(status='complete_disassembly', reason=None, stage='complete_candidate')
        persist()
        clock()
        out.close()
        out = None
        clock()
        return 0, b''
    except BaseException as error:
        if report is not None:
            report.update(status='refused', reason=_reason(error), instructions=None,
                          incoming_instruction_starts_verified=False, verified_incoming_offsets=None)
            try:
                _reap(process, report)
                if process is not None and report['child_reaped']:
                    capture()
                if out is not None and 'report.json' not in expected:
                    persist()
            except BaseException:
                pass
        return 1, DIAGNOSTIC
    finally:
        if out is not None:
            try:
                out.close()
            except BaseException:
                pass
