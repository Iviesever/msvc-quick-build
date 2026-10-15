"""Join closed-owner bytes to a separately pinned controller receipt.

This is a finite evidence consumer, not a process observer, a signature verifier,
a permission allocator or an independent semantic review of x86 instructions.
Both expected digests must be obtained through the trusted external ledger.
Never compute them from untrusted input and call that authentication.
"""
from __future__ import annotations
from bcrypt_admission_contract_017 import (LIMITS, REPORT_CAP, RECEIPT_CAP,
    decode, equal, fields, fingerprint, join_evidence, validate_request)
from bcrypt_guarded_owner_017 import CAPTURE_CAP, checked_listing, need
from bcrypt_owner_backend_017 import sha256_text

ARTIFACT_CAPS = {'consumed.json':1024, 'target.bin':103,
                'tool-stdout.bin':CAPTURE_CAP-1, 'tool-stderr.bin':0,
                'report.json':REPORT_CAP, 'owner-final.json':REPORT_CAP}
_TRUE = ('reservation_attempted','reserved','child_obtained','child_reaped',
         'child_close_attempted','child_closed','output_close_attempted','output_closed',
         'candidate_written','capture_attempted','capture_stable','completion_requires_outer_receipt')
_FALSE = ('cleanup_failure','persistence_failure','launch_outcome_unknown','kill_attempted',
          'capture_failure','execution_admitted','source_independently_reviewed',
          'runtime_loader_verified','python_module_bytes_verified','original_lineage_verified',
          'descendant_cleanup_verified','observation_accepted','clears_hold')
_OTHER = ('schema','status','first_failure','launch_reservations','child_returncode','boundary',
          'prepared_sha256','raw_stdout','raw_stderr','instructions','cause','binding','verification')


def evaluate_completion(request_raw, expected_request_sha, *, metadata, receipts,
                        artifacts, controller_raw, expected_controller_sha):
    """Return transport consistency only; raise on ANY missing or mismatched fact.

    `owner-final.json` is an outer-owned record, not a sixth file in the backend's
    exclusive directory. `report.json` is immutable and must remain the earlier
    pre-close candidate. The controller must observe a containing clean exit and
    stable readback after all writers terminate, rather than copying child fields.
    """
    request=validate_request(request_raw,expected_request_sha)
    binding=join_evidence(request,metadata,receipts)
    binding['request_sha256']=expected_request_sha
    c=request['context']
    fields(artifacts,ARTIFACT_CAPS)
    # All lengths are checked BEFORE hashing any untrusted capture buffer.
    for name, cap in ARTIFACT_CAPS.items():
        need(type(artifacts[name]) is bytes and len(artifacts[name]) <= cap, 'RECEIPT_SIZE')
    need(len(artifacts['target.bin']) == 103 and artifacts['tool-stdout.bin'], 'RECEIPT_TARGET')
    sha256_text(expected_controller_sha)
    need(type(controller_raw) is bytes and 0 < len(controller_raw) <= RECEIPT_CAP, 'RECEIPT_SIZE')
    need(fingerprint(controller_raw)['sha256'] == expected_controller_sha, 'RECEIPT_ROOT')
    receipt=decode(controller_raw,RECEIPT_CAP)
    fields(receipt, ('schema','binding','controller','artifacts','owner_exit_code','owner_reaped',
                     'timed_out','interrupted','writers_quiescent','readback_verified','elapsed_ns'))
    equal(receipt['schema'],'bcrypt-outer-completion-017-v1')
    equal(receipt['binding'],binding)
    controller=next(ref for ref in c['files'].values() if ref['path'] == c['programs']['controller'])
    equal(receipt['controller'],controller)
    equal(receipt['artifacts'],{name:fingerprint(raw) for name,raw in artifacts.items()})
    equal(receipt['owner_exit_code'],0)
    for name in ('owner_reaped','writers_quiescent','readback_verified'): equal(receipt[name],True)
    for name in ('timed_out','interrupted'): equal(receipt[name],False)
    elapsed=receipt['elapsed_ns']
    need(type(elapsed) is int and 0 <= elapsed < LIMITS['outer_seconds']*1_000_000_000,
         'RECEIPT_DEADLINE')
    equal(decode(artifacts['consumed.json'],1024),
          dict(schema='bcrypt-owner-reservation-017-v1',request_sha256=expected_request_sha,
               target_sha256=c['target']['sha256'],retry_allowed=False,execution_admitted=False))
    target=artifacts['target.bin']
    equal(fingerprint(target),dict(bytes=103,sha256=c['target']['sha256']))
    final=decode(artifacts['owner-final.json'],REPORT_CAP)
    fields(final,(*_TRUE,*_FALSE,*_OTHER))
    for name in _TRUE: equal(final[name],True)
    for name in _FALSE: equal(final[name],False)
    equal(final['schema'],'bcrypt-guarded-owner-017-v1')
    equal(final['status'],'owner_complete_unreviewed')
    equal(final['launch_reservations'],1); equal(final['child_returncode'],0)
    equal(final['first_failure'],None); equal(final['cause'],None)
    equal(final['prepared_sha256'],c['target']['sha256']); equal(final['binding'],binding)
    equal(final['raw_stdout'],fingerprint(artifacts['tool-stdout.bin']))
    equal(final['raw_stderr'],fingerprint(artifacts['tool-stderr.bin']))
    checked_listing(final['instructions'],target)
    equal(final['boundary'],dict(schema='bcrypt-import-boundary-017-v1',status='closed_unchanged',
          reason=None,finder_detached=True,module_count=len(c['modules']),execution_admitted=False,
          source_independently_reviewed=False,runtime_loader_verified=False,
          python_module_bytes_verified=False,python_stdlib_and_dynamic_imports_qualified=False))
    refs=[*c['files'].values(),*c['metadata'].values(),*request['receipts'].values()]
    pass_counts=dict(files=len(refs),bytes=sum(r['bytes'] for r in refs),eof_requests=len(refs))
    equal(final['verification'],dict(before=pass_counts,after=pass_counts,failure=None))
    # Exactly five fields change between a successful candidate and closed owner.
    expected_candidate=dict(final,status='body_complete_unreviewed',candidate_written=False,
                            output_close_attempted=False,output_closed=False,boundary=None)
    equal(decode(artifacts['report.json'],REPORT_CAP),expected_candidate)
    return dict(schema='bcrypt-completion-assessment-017-v1',
                status='CONSISTENT_TRANSPORT_REQUIRES_RESULT_REVIEW',binding=binding,
                controller_receipt_sha256=expected_controller_sha,transport_complete=True,
                observation_accepted=False,native_execution_performed=False,
                clears_hold=False,cause=None)
