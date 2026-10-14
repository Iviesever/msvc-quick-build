"""017 prepared-target lifecycle; no CLI, native launcher or execution allocation.

The caller must separately qualify/freeze the two preparations, source/module
bytes, loader, bounded child adapter and outer evidence controller. This internal
body NEVER upgrades a caller's bytes/callbacks into that qualification. Tests use
only synthetic preparations and an in-process child. No historical owner runs.
"""
from __future__ import annotations
import hashlib
import math
import sys
import time

from bcrypt_import_boundary_017 import ImportBoundary, Refused

RVA_START, RVA_END, IMAGE_BASE = 0x1117C, 0x111E3, 0x180000000
TARGET_BYTES = RVA_END - RVA_START
CAPTURE_CAP, RECORD_CAP, ASSEMBLY_CAP = 131072, 103, 384
BODY_SECONDS, CLEANUP_SECONDS = 12, 1
REASONS = frozenset(('PREPARATION', 'CAPTURE', 'CHILD_EXIT', 'CHILD_STDERR',
                     'LISTING', 'DEADLINE', 'CLEANUP', 'OUTPUT', 'INTERNAL',
                     'INTERRUPTED', 'IMPORT_BOUNDARY', 'REUSED'))


def need(ok, code):
    if not ok:
        raise Refused(code)


def digest(raw):
    return dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def prepared_target(primary, independent, expected_sha):
    """Agreement of already prepared bytes, NOT proof of independent preparation."""
    need(type(primary) is bytes and type(independent) is bytes and
         len(primary) == len(independent) == TARGET_BYTES, 'PREPARATION')
    need(type(expected_sha) is str and len(expected_sha) == 64 and
         all(c in '0123456789abcdef' for c in expected_sha), 'PREPARATION')
    need(primary == independent and digest(primary)['sha256'] == expected_sha,
         'PREPARATION')
    return primary


def checked_listing(rows, target):
    """Check decoder-adapter projection, never decode instructions or follow them."""
    need(type(target) is bytes and len(target) == TARGET_BYTES, 'LISTING')
    need(type(rows) is list and 1 <= len(rows) <= RECORD_CAP, 'LISTING')
    expected_keys = {'rva', 'length', 'raw_hex', 'assembly'}
    cursor, result = 0, []
    for row in rows:
        need(type(row) is dict and row.keys() == expected_keys, 'LISTING')
        size, address = row['length'], row['rva']
        need(type(size) is int and 1 <= size <= 15 and type(address) is int and
             address == RVA_START + cursor and cursor + size <= TARGET_BYTES,
             'LISTING')
        raw_hex, assembly = row['raw_hex'], row['assembly']
        need(type(raw_hex) is str and raw_hex == target[cursor:cursor+size].hex(), 'LISTING')
        need(type(assembly) is str and 1 <= len(assembly) <= ASSEMBLY_CAP and
             all(32 <= ord(c) < 127 for c in assembly), 'LISTING')
        result.append(dict(row))  # Own the bounded projection, not adapter dicts.
        cursor += size
    need(cursor == TARGET_BYTES, 'LISTING')
    return result


def reason(error):
    if isinstance(error, (KeyboardInterrupt, SystemExit)):
        return 'INTERRUPTED'
    if isinstance(error, Refused):
        # Never serialize arbitrary exception text, paths, manifests or argv.
        code = error.args[0] if len(error.args) == 1 else None
        return code if type(code) is str and len(code) <= 32 and code in REASONS else 'IMPORT_BOUNDARY'
    return 'INTERNAL'


class GuardedPreparedOwner:
    """Single-use orchestration with an explicit trusted, bounded adapter seam.

    sink: reserve(), write(name, bytes), candidate(report), close(). All creates
    must be exclusive; failed prefixes retained. candidate must persist an owned
    snapshot, not keep a mutable dict. It NEVER certifies clean owner completion.

    launch(target): atomically own one child or raise without launching. If the
    OS cannot guarantee that contract, the OUTER controller must preserve unknown
    ownership. child: wait(timeout), poll(), kill(), capture(cap), close(). wait /
    poll must reap (not merely detect exit); kill addresses only this owned child.
    capture returns (stdout, stderr) after reap, each within the passed cap. No
    pipe communicate()/unbounded allocation, descendant-cleanup or OS deadline
    guarantee is implied by these interface calls. No adapter is installed here.
    """
    def __init__(self, expected_modules, *, runtime=sys, clock=time.monotonic):
        self.boundary = ImportBoundary(expected_modules, runtime=runtime)
        self.clock, self.used = clock, False

    def run(self, primary, independent, expected_sha, *, sink, launch, parse, verify):
        # A second run must not touch any sink, child, preparation or interpreter.
        need(not self.used, 'REUSED')
        self.used = True
        report = dict(schema='bcrypt-guarded-owner-017-v1', status='refused',
                      first_failure=None, cleanup_failure=False, persistence_failure=False,
                      reservation_attempted=False, reserved=False, launch_reservations=0, child_obtained=False,
                      launch_outcome_unknown=False,
                      child_reaped=False, child_returncode=None, kill_attempted=False,
                      child_close_attempted=False, child_closed=False,
                      output_close_attempted=False, output_closed=False,
                      candidate_written=False, boundary=None, prepared_sha256=None,
                      raw_stdout=None, raw_stderr=None, capture_attempted=False,
                      capture_stable=False, capture_failure=False, instructions=None,
                      completion_requires_outer_receipt=True, execution_admitted=False,
                      source_independently_reviewed=False, runtime_loader_verified=False,
                      python_module_bytes_verified=False, original_lineage_verified=False,
                      descendant_cleanup_verified=False, observation_accepted=False,
                      clears_hold=False, cause=None)
        child, stdout, stderr, start, stage = None, None, None, None, 'activation'

        def fail(error, code=None):
            if report['first_failure'] is None:
                report['first_failure'] = code or reason(error)
            report['status'], report['instructions'] = 'refused', None

        def now():
            value = self.clock()
            need(type(value) in (int, float) and 0 <= value <= 2**53 and math.isfinite(value), 'DEADLINE')
            return value

        def checkpoint():
            self.boundary.checkpoint()
            value = now()
            need(start <= value < start + BODY_SECONDS, 'DEADLINE')
            return max(0.0, start + BODY_SECONDS - value)

        def reaped(code):
            need(type(code) is int, 'CLEANUP')  # bool is not a return code.
            report['child_returncode'], report['child_reaped'] = code, True

        def capture():
            nonlocal stdout, stderr
            if report['capture_attempted'] or not report['child_reaped']:
                return
            report['capture_attempted'] = True
            try:
                stdout, stderr = child.capture(CAPTURE_CAP)
                need(type(stdout) is bytes and type(stderr) is bytes and
                     len(stdout) < CAPTURE_CAP and len(stderr) < CAPTURE_CAP, 'CAPTURE')
                report['raw_stdout'], report['raw_stderr'] = digest(stdout), digest(stderr)
                report['capture_stable'] = True
                sink.write('stdout.bin', stdout)
                sink.write('stderr.bin', stderr)
            except BaseException:
                report['capture_failure'] = True
                raise

        try:
            start = now()
            self.boundary.activate()
            checkpoint()
            # Reserve before body work. Failure consumes this owner object; a
            # persistent cross-owner reservation is the separately qualified sink.
            stage = 'reserve'
            report['reservation_attempted'] = True
            sink.reserve()
            report['reserved'] = True
            checkpoint()
            target = prepared_target(primary, independent, expected_sha)
            report['prepared_sha256'] = expected_sha
            need(verify('before') is None, 'PREPARATION')  # raises on source/module/lineage drift; not a bool.
            checkpoint()
            stage = 'output'
            sink.write('target.bin', target)
            checkpoint()
            stage = 'launch'
            report['launch_reservations'] = 1
            child = launch(target)
            need(child is not None, 'CLEANUP')
            report['child_obtained'] = True
            # Assign child BEFORE this check: refusal must still reap that child.
            remaining = checkpoint()
            stage = 'wait'
            reaped(child.wait(timeout=remaining))
            checkpoint()
            stage = 'capture'
            capture()
            checkpoint()
            need(report['child_returncode'] == 0, 'CHILD_EXIT')
            need(not stderr, 'CHILD_STDERR')
            stage = 'parse'
            report['instructions'] = checked_listing(parse(stdout, target), target)
            checkpoint()
            need(verify('after') is None, 'PREPARATION')
            checkpoint()
            report['status'] = 'body_complete_unreviewed'
        except BaseException as error:
            if stage == 'launch' and child is None:
                report['launch_outcome_unknown'] = True
            code = reason(error)
            if code == 'INTERNAL' and stage in ('reserve', 'output'):
                code = 'OUTPUT'
            elif code == 'INTERNAL' and stage == 'capture':
                code = 'CAPTURE'
            fail(error, code)
        finally:
            # Cleanup does not use the possibly-refused boundary or success
            # deadline. It never launches, retries or resets an observation.
            if child is not None:
                try:
                    if not report['child_reaped']:
                        code = child.poll()
                        if code is None:
                            report['kill_attempted'] = True
                            try:
                                child.kill()
                            except BaseException as error:
                                report['cleanup_failure'] = True
                                fail(error, 'CLEANUP')
                            reaped(child.wait(timeout=CLEANUP_SECONDS))
                        else:
                            reaped(code)
                except BaseException as error:
                    report['cleanup_failure'] = True
                    fail(error, 'CLEANUP')
                try:
                    capture()  # Preserve post-reap failure output; never retry capture.
                except BaseException as error:
                    fail(error, 'CAPTURE')
                try:
                    report['child_close_attempted'] = True
                    child.close()
                    report['child_closed'] = True
                except BaseException as error:
                    report['cleanup_failure'] = True
                    fail(error, 'CLEANUP')
            try:
                checkpoint()
            except BaseException as error:
                fail(error)
            # Preserve a refused record even with a latched import refusal; the
            # sink must already be preloaded. No new imports or retry are allowed.
            if report['reservation_attempted']:
                try:
                    sink.candidate(report)
                    report['candidate_written'] = True
                except BaseException as error:
                    report['persistence_failure'] = True
                    fail(error, 'OUTPUT')
                try:
                    report['output_close_attempted'] = True
                    sink.close()
                    report['output_closed'] = True
                except BaseException as error:
                    report['persistence_failure'] = True
                    fail(error, 'OUTPUT')
            try:
                checkpoint()  # Includes mutations caused by persistence/close.
            except BaseException as error:
                fail(error)
            try:
                report['boundary'] = self.boundary.close()
                need(report['boundary']['status'] == 'closed_unchanged' and
                     report['boundary']['finder_detached'] is True, 'IMPORT_BOUNDARY')
            except BaseException as error:
                fail(error)
        if report['first_failure'] is None:
            report['status'] = 'owner_complete_unreviewed'
        return report
