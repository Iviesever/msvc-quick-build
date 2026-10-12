"""One separately consumed selection from one verified image snapshot."""
import hashlib
import os
import stat
import time

from bcrypt_disassembly_model_016 import (
    Refusal, require, encode_json, validate_manifest, SELECTION_LIMITS as LIMITS,
)
from bcrypt_selection_016 import select_target
from bcrypt_disassembly_io_014 import OutputDirectory, verify_identity, verify_environment

DIAGNOSTIC = b'PREPARATION_REFUSED\n'


class PreparationDirectory(OutputDirectory):
    @staticmethod
    def _caps():
        return {'consumed.json': 1024, 'target.bin': 83, 'report.json': 8192}


def _image_state(path):
    require(type(path) is str and 1 < len(path) <= 256 and os.path.isabs(path)
            and os.path.normpath(path) == path and os.path.realpath(path) == path,
            'IMAGE_PATH')
    value = os.stat(path, follow_symlinks=False)
    require(stat.S_ISREG(value.st_mode), 'IMAGE_REGULAR')
    return (value.st_dev, value.st_ino, value.st_mode, value.st_nlink, value.st_size,
            value.st_mtime_ns, value.st_ctime_ns)


def _reason(error):
    code = error.code if isinstance(error, Refusal) else 'IO_OR_INTERNAL_ERROR'
    return code if (type(code) is str and 1 <= len(code) <= 48 and
                    all(c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789' for c in code)) else 'IO_OR_INTERNAL_ERROR'


def observe(manifest, manifest_raw, manifest_path, expected_sha, start_ns):
    out, report, image_state = None, None, None
    report_attempted, expected = False, {}
    deadline = start_ns + LIMITS['owner_seconds'] * 1_000_000_000

    def clock():
        now = time.monotonic_ns()
        require(type(start_ns) is int and type(now) is int
                and 0 <= start_ns <= now <= deadline, 'DEADLINE')
        return now

    def identities(before):
        nonlocal image_state
        clock()
        verify_identity({'path': manifest_path, 'bytes': len(manifest_raw),
                         'sha256': expected_sha}, LIMITS['manifest_bytes'])
        verify_environment(manifest)
        retained = {}
        for role, record in manifest['identities'].items():
            clock()
            if role == 'image':
                if not before:
                    require(_image_state(record['path']) == image_state, 'IMAGE_CHANGED')
                    report['image_snapshot_stat_stable'] = True
                    continue
                image_state = _image_state(record['path'])
                report['image_read_reservations'] = 1
            keep = before and role in ('image', 'index', 'admission', 'caller')
            raw = verify_identity(record, LIMITS.get(role + '_bytes', LIMITS['identity_bytes']), keep)
            if keep:
                retained[role] = raw
            if role == 'image':
                report['original_image_full_reads'] = 1
        for group, cap in (('provenance', LIMITS['provenance_bytes']),
                           ('sources', LIMITS['source_bytes'])):
            for record in manifest[group].values():
                clock()
                verify_identity(record, cap)
        clock()
        return retained

    def persist():
        nonlocal report_attempted
        report_attempted = True
        report['elapsed_ns_before_persistence'] = max(0, time.monotonic_ns() - start_ns)
        raw = encode_json(report, LIMITS['report_bytes'])
        expected['report.json'] = raw
        out.create('report.json', raw, LIMITS['report_bytes'])
        out.durable_readback(expected)

    try:
        clock()
        validate_manifest(manifest)
        require(manifest['phase'] == 'selection', 'PREPARATION_PHASE')
        require(type(manifest_raw) is bytes and
                encode_json(manifest, LIMITS['manifest_bytes']) == manifest_raw
                and hashlib.sha256(manifest_raw).hexdigest() == expected_sha, 'MANIFEST_IDENTITY')
        out = PreparationDirectory(manifest['output_dir'])
        report = {
            'schema': 'bcrypt-selection-report-016-v1', 'status': 'refused',
            'reason': 'NOT_COMPLETE', 'stage': 'reserved', 'consumed': True,
            'call_id': manifest['call_id'], 'mode': manifest['mode'],
            'baseline_commit': manifest['baseline_commit'], 'source_commit': manifest['source_commit'],
            'manifest_sha256': expected_sha, 'target': dict(manifest['target']),
            'input_identities': {role: manifest['identities'][role]
                                 for role in ('image', 'index', 'admission', 'caller')},
            'image_read_reservations': 0, 'original_image_full_reads': 0,
            'selection_from_verified_snapshot': False, 'image_snapshot_stat_stable': False,
            'chain_metadata_snapshot_verified': False, 'chain_metadata_bytes': 0,
            'incoming_branches_verified': False,
            'identities_before_verified': False, 'identities_after_verified': False,
            'tool_processes': 0, 'native_execution_performed': False,
            'completion_requires_clean_owner_exit': True,
        }
        marker = encode_json({
            'schema': 'bcrypt-selection-consumed-016-v1', 'consumed': True,
            'call_id': manifest['call_id'], 'mode': manifest['mode'],
            'baseline_commit': manifest['baseline_commit'], 'source_commit': manifest['source_commit'],
            'manifest_sha256': expected_sha, 'attempt_reuse_allowed': False,
        }, LIMITS['marker_bytes'])
        expected['consumed.json'] = marker
        out.create('consumed.json', marker, LIMITS['marker_bytes'])
        report['stage'] = 'identities_before'
        inputs = identities(True)
        report['identities_before_verified'] = True
        report['stage'] = 'selection'
        target = select_target(manifest, inputs['image'], inputs['index'], inputs['admission'], inputs['caller'])
        del inputs
        require(type(target) is bytes and 0 < len(target) <= LIMITS['target_bytes'], 'TARGET_SIZE')
        report['target']['sha256'] = hashlib.sha256(target).hexdigest()
        report['selection_from_verified_snapshot'] = True
        report.update(chain_metadata_snapshot_verified=True, chain_metadata_bytes=52,
                      incoming_branches_verified=True)
        expected['target.bin'] = target
        out.create('target.bin', target, LIMITS['target_bytes'])
        clock()
        report['stage'] = 'identities_after'
        identities(False)
        report['identities_after_verified'] = True
        report.update(status='complete_selection', reason=None, stage='complete_candidate')
        persist()
        clock()
        out.close()
        out = None
        clock()
        return 0, b''
    except BaseException as error:
        if report is not None:
            report.update(status='refused', reason=_reason(error))
            if out is not None and not report_attempted:
                try:
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
