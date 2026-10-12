"""One reserved tool process, exact static bytes, bounded capture and durability."""
import hashlib
import os
import signal
import subprocess
import time

from bcrypt_disassembly_model_014 import (
    LIMITS, Refusal, encode_json, require, select_target, validate_manifest,
)
from bcrypt_disassembly_listing_014 import parse_listing
from bcrypt_disassembly_io_014 import (
    OutputDirectory, child_limits, verify_environment, verify_identity,
)

DIAGNOSTIC = b'OBSERVATION_REFUSED\n'


def _digest(raw):
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def _reason(error):
    code = error.code if isinstance(error, Refusal) else 'IO_OR_INTERNAL_ERROR'
    if (type(code) is str and 1 <= len(code) <= 48 and
            all(c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789' for c in code)):
        return code
    return 'IO_OR_INTERNAL_ERROR'


def _reap(process, report):
    """Failure cleanup does not create a new success deadline or another child."""
    if process is None or report['child_reaped']:
        return
    # wait() may have reaped before propagating an interrupt. Never signal a
    # process-group ID after that known ownership has ended.
    returncode = process.poll()
    if returncode is not None:
        report['child_returncode'] = returncode
        report['child_reaped'] = True
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
    expected, target = {}, None
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
        retained = {}
        for role, record in manifest['identities'].items():
            clock()
            needed = keep and role in ('image', 'index', 'admission')
            raw = verify_identity(record, LIMITS.get(role + '_bytes',
                                  LIMITS['identity_bytes']), keep=needed)
            if needed:
                retained[role] = raw
        for record in manifest['sources'].values():
            clock()
            verify_identity(record, LIMITS['source_bytes'])
        clock()
        return retained

    def capture():
        if not report['child_reaped']:
            return
        for name, key in (('tool-stdout.bin', 'raw_stdout'),
                          ('tool-stderr.bin', 'raw_stderr')):
            raw = out.read(name, LIMITS['child_file_bytes'])
            expected[name] = raw
            report[key] = _digest(raw)
        report['capture_stable'] = True

    def persist():
        report['elapsed_ns_before_persistence'] = max(0, time.monotonic_ns() - start_ns)
        wrapper = dict(report, instructions=None)
        encode_json(wrapper, LIMITS['report_wrapper_bytes'])
        raw = encode_json(report, LIMITS['report_bytes'])
        out.create('report.json', raw, LIMITS['report_bytes'])
        expected['report.json'] = raw
        # Unreaped capture is mutable: no stable digest or full durability claim.
        if report['capture_stable'] or process is None:
            out.durable_readback(expected)

    try:
        clock()
        validate_manifest(manifest)
        require(type(manifest_raw) is bytes and
                encode_json(manifest, LIMITS['manifest_bytes']) == manifest_raw and
                hashlib.sha256(manifest_raw).hexdigest() == expected_sha, 'MANIFEST_IDENTITY')
        out = OutputDirectory(manifest['output_dir'])
        report = {
            'schema': 'bcrypt-disassembly-report-014-v1', 'status': 'refused',
            'reason': 'NOT_COMPLETE', 'stage': 'reserved', 'consumed': True,
            'mode': manifest['mode'], 'call_id': manifest['call_id'],
            'source_commit': manifest['source_commit'], 'manifest_sha256': expected_sha,
            'target': manifest['target'], 'child_launch_reservations': 0,
            'child_handle_obtained': False, 'child_pid': None,
            'child_reaped': False, 'child_returncode': None, 'capture_stable': False,
            'identities_before_verified': False, 'identities_after_verified': False,
            'raw_stdout': None, 'raw_stderr': None, 'instructions': None,
            'completion_requires_clean_owner_exit': True,
            'native_execution_performed': False, 'outgoing_targets_followed': False,
        }
        marker = encode_json({
            'schema': 'bcrypt-disassembly-consumed-014-v1', 'consumed': True,
            'call_id': manifest['call_id'], 'mode': manifest['mode'],
            'source_commit': manifest['source_commit'], 'manifest_sha256': expected_sha,
            'attempt_reuse_allowed': False,
        }, LIMITS['marker_bytes'])
        expected['consumed.json'] = marker
        out.create('consumed.json', marker, LIMITS['marker_bytes'])
        report['stage'] = 'identities_before'
        inputs = identities(keep=True)
        target = select_target(manifest, inputs['image'], inputs['index'], inputs['admission'])
        del inputs
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
        process = subprocess.Popen(
            manifest['tool_argv'], cwd=manifest['output_dir'],
            env=manifest['environment'], stdin=subprocess.DEVNULL,
            stdout=stdout_fd, stderr=stderr_fd, close_fds=True,
            start_new_session=True, restore_signals=True, preexec_fn=child_limits,
        )
        report['child_handle_obtained'] = True
        report['child_pid'] = process.pid
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
        report['instructions'] = parse_listing(expected['tool-stdout.bin'], target,
                                                manifest['target'])
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
            report.update(status='refused', reason=_reason(error), instructions=None)
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
