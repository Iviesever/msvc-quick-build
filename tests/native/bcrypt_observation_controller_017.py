"""One fixed bootstrap -> scoped transport -> six-file completion join.

Writer quiescence is conditional on an independently pinned, exact output-domain
qualification PLUS observed clean descendant drain. No writer authority is
synthesized here. A returned assessment is not a semantic observation approval;
this controller's containing clean exit still needs independent retention.
"""
from pathlib import Path
import os
import stat
import sys
import time

if __name__ == '__main__':
    if not (sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode and not sys.flags.optimize):
        raise SystemExit('CONTROLLER_ISOLATION_REQUIRED')
    sys.path.insert(0,str(Path(__file__).resolve().parent))

from bcrypt_bootstrap_plan_017 import BootstrapInputs, record_entry_preload
from bcrypt_admission_contract_017 import encode, equal, fingerprint, RECEIPT_CAP
from bcrypt_completion_receipt_017 import evaluate_completion
from bcrypt_guarded_owner_017 import Refused, need
from bcrypt_scoped_transport_017 import ScopedOwnerTransport
from bcrypt_outer_transport_017 import _stamp

NAMES = frozenset(('reservation.json','scope.json','completion.json','assessment.json','failure.json'))


class ControllerRecords:
    """Five bounded immutable local records. No overwrite, delete, or cleanup retry."""
    def __init__(self,path):
        self.path = Path(path); self.fd = None; self.closed = False
        self.saved = {}  # Owned bytes and post-write identity, at most five records.
        need(str(self.path.parent.resolve(strict=True)) == str(self.path.parent), 'CONTROL_PATH')
        parent = os.open(self.path.parent,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
        try:
            os.mkdir(self.path.name,0o700,dir_fd=parent)
            self.fd = os.open(self.path.name,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=parent)
            self.identity = _stamp(os.fstat(self.fd))[:4]
            os.fsync(parent)
        except BaseException:
            if self.fd is not None: os.close(self.fd); self.fd = None
            raise
        finally: os.close(parent)

    def check(self):
        need(not self.closed and self.fd is not None and os.path.realpath(self.path) == str(self.path),
             'CONTROL_CLOSED')
        need(self.identity == _stamp(os.fstat(self.fd))[:4] == _stamp(self.path.lstat())[:4], 'CONTROL_PATH')
        # A permitted filename is not evidence that this instance created it.
        with os.scandir(self.fd) as entries:
            found = set()
            for entry in entries:
                need(entry.name in self.saved and entry.name not in found, 'CONTROL_MEMBERS')
                found.add(entry.name)
        need(found == self.saved.keys(), 'CONTROL_MEMBERS')
        for name, (_, stamp) in self.saved.items():
            need(_stamp(os.stat(name,dir_fd=self.fd,follow_symlinks=False)) == stamp, 'CONTROL_FILE')

    def save(self,name,raw):
        self.check()
        need(name in NAMES and type(raw) is bytes and 0 < len(raw) <= RECEIPT_CAP, 'CONTROL_SIZE')
        fd = os.open(name,os.O_RDWR|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=self.fd)
        try:
            st = os.fstat(fd)
            need(stat.S_ISREG(st.st_mode) and st.st_nlink == 1 and st.st_size == 0, 'CONTROL_FILE')
            cursor = 0
            while cursor < len(raw):
                n = os.write(fd,raw[cursor:]); need(n > 0, 'CONTROL_WRITE'); cursor += n
            os.fsync(fd)
            need(os.pread(fd,len(raw)+1,0) == raw, 'CONTROL_READBACK')
            need(_stamp(os.fstat(fd)) == _stamp(os.stat(name,dir_fd=self.fd,follow_symlinks=False)),
                 'CONTROL_FILE')
            self.saved[name] = (raw, _stamp(os.fstat(fd)))
        finally: os.close(fd)
        os.fsync(self.fd); self.check()

    def verify(self):
        """One final content pass, <=5 files / 163840 bytes plus five EOF bytes.

        Existing per-save fsync/readback is retained. Recheck ALL earlier files
        before reporting directory closure, not only the last-written record.
        This is bounded change detection, not an atomic external-writer lock.
        """
        self.check()
        for name, (raw, stamp) in self.saved.items():
            fd = os.open(name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=self.fd)
            try:
                need(_stamp(os.fstat(fd)) == stamp, 'CONTROL_FILE')
                need(os.pread(fd,len(raw)+1,0) == raw, 'CONTROL_READBACK')
                need(_stamp(os.fstat(fd)) == stamp ==
                     _stamp(os.stat(name,dir_fd=self.fd,follow_symlinks=False)), 'CONTROL_FILE')
            finally: os.close(fd)
        self.check()

    def close(self):
        need(not self.closed, 'CONTROL_CLOSED')
        failure = None
        try: self.verify()
        except BaseException as error: failure = error
        self.closed = True
        fd,self.fd = self.fd,None
        try:
            if fd is not None: os.close(fd)
        except BaseException as error:
            if failure is None: failure = error
        if failure is not None: raise failure


def _observed_success(result):
    need(type(result) is dict and result['status'] == 'scoped_transport_complete_unreviewed', 'CONTROL_SCOPE')
    direct, scope = result['transport'],result['scope']
    for key in ('launch_reserved','owner_reaped','pipes_eof','capture_complete','final_json',
                'readback_verified','output_closed'): equal(direct[key],True)
    for key in ('launch_outcome_unknown','timed_out','interrupted','kill_attempted',
                'cleanup_failed','persistence_failed'): equal(direct[key],False)
    equal(direct['owner_exit_code'],0); equal(direct['reason'],None)
    equal(direct['status'],'owner_transport_complete_unreviewed')
    equal(scope['status'],'descendant_scope_complete_unreviewed'); equal(scope['reason'],None)
    for key in ('activated','descendant_domain_empty','policy_restored'): equal(scope[key],True)
    for key in ('unexpected_children','cleanup_failed'): equal(scope[key],False)
    equal(scope['signal_attempts'],0); equal(scope['children_reaped'],0); equal(scope['exit_codes'],[])


class ObservationController:
    def __init__(self,plan_path,expected_plan_sha):
        self.path,self.sha,self.used = plan_path,expected_plan_sha,False

    def run(self):
        need(not self.used,'REUSED'); self.used = True
        started = time.monotonic_ns(); records = wrapper = inputs = None
        stage = 'inputs'
        result = dict(schema='bcrypt-fixed-controller-result-017-v1',status='refused',reason=None,
            plan_sha256=self.sha,launch_attempted=False,scope=None,receipt_sha256=None,assessment=None,
            identity_before=None,identity_after=None,
            output_closed=False,persistence_failed=False,elapsed_ns=None,
            requires_clean_controller_exit=True,observation_accepted=False,clears_hold=False)
        def clock():
            elapsed = time.monotonic_ns()-started
            need(0 <= elapsed < 16_000_000_000,'CONTROL_DEADLINE')
            return elapsed
        try:
            inputs = BootstrapInputs(self.path,self.sha)
            p,c = inputs.plan,inputs.request['context']
            need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode and
                 not sys.flags.optimize and os.path.realpath(__file__) == inputs.controller['path'] and
                 os.path.realpath(sys.executable) == c['programs']['python'], 'CONTROL_RUNTIME')
            inputs.verify_control_sources()
            result['identity_before'] = inputs.verify_launch_files(); clock()
            stage = 'save'
            records = ControllerRecords(p['receipt_dir'])
            records.save('reservation.json',encode(dict(schema='bcrypt-controller-reservation-017-v1',
                plan_sha256=self.sha,request_sha256=p['request']['sha256'],attempts=1,
                retry_allowed=False,observation_accepted=False),RECEIPT_CAP))
            stage = 'launch'
            launch = encode(inputs.launch_spec()); wrapper = ScopedOwnerTransport(launch,fingerprint(launch)['sha256'])
            clock(); result['launch_attempted'] = True
            observed = wrapper.run(); result['scope'] = observed
            stage = 'save'
            records.save('scope.json',encode(dict(observed=observed,
                authority_sha256=p['authority']['sha256'],authority_is_external_premise=True),RECEIPT_CAP))
            stage = 'readback'
            _observed_success(observed); clock()
            # Raw candidate is never changed into a post-close result.
            artifacts,hashes = wrapper.read_artifacts(c['output_dir'])
            checked = inputs.recheck(); checked.verify_control_sources()
            result['identity_after'] = checked.verify_launch_files()
            wrapper.scope.assert_empty(); elapsed = clock()
            receipt = dict(schema='bcrypt-outer-completion-017-v1',binding=inputs.binding,
                controller=inputs.controller,artifacts=hashes,owner_exit_code=observed['transport']['owner_exit_code'],
                owner_reaped=observed['transport']['owner_reaped'],timed_out=False,interrupted=False,
                writers_quiescent=True,readback_verified=True,elapsed_ns=elapsed)
            # True above is conditional on the pinned external writer premises,
            # clean observed scope, immutable output ownership and checked readback.
            raw = encode(receipt,RECEIPT_CAP); receipt_sha = fingerprint(raw)['sha256']
            assessment = evaluate_completion(inputs.request_raw,p['request']['sha256'],
                metadata=inputs.metadata,receipts=inputs.receipts,artifacts=artifacts,
                controller_raw=raw,expected_controller_sha=receipt_sha)
            stage = 'save'
            records.save('completion.json',raw)
            records.save('assessment.json',encode(assessment,RECEIPT_CAP))
            stage = 'final_check'
            wrapper.scope.assert_empty(); clock()
            result.update(status='bootstrap_transport_complete_unreviewed',
                          receipt_sha256=receipt_sha,assessment=assessment)
        except BaseException as error:
            if stage == 'save': result['persistence_failed'] = True
            result['reason'] = ('INTERRUPTED' if isinstance(error,(KeyboardInterrupt,SystemExit))
                else 'CONTROL_REFUSED' if isinstance(error,Refused) else 'CONTROL_IO')
            if records is not None:
                try: records.save('failure.json',encode(result,RECEIPT_CAP))
                except BaseException: result['persistence_failed'] = True
        finally:
            if records is not None:
                try: records.close(); result['output_closed'] = True
                except BaseException:
                    result['persistence_failed'] = True
                    result['reason'] = result['reason'] or 'CONTROL_CLOSE'
            try:
                if wrapper is not None and result['status'] == 'bootstrap_transport_complete_unreviewed':
                    wrapper.scope.assert_empty()
                result['elapsed_ns'] = clock()
            except BaseException:
                result['reason'] = result['reason'] or 'CONTROL_FINAL_CHECK'
        if result['reason'] is not None:
            result['status'] = 'refused'
            # On-disk candidates are retained unchanged; no success API result.
            result['assessment'] = None
        return result


def main(argv):
    try:
        need(type(argv) is list and len(argv) in (2, 3), 'CONTROL_ARGV')
        if len(argv) == 2:
            need(argv[1] == '--inspect-preload' and
                 os.path.realpath(argv[0]) == os.path.realpath(__file__), 'CONTROL_ARGV')
            raw = record_entry_preload('controller', __file__)
            need(os.write(1, raw) == len(raw), 'CONTROL_STDOUT')
            return 0
        need(argv[1] != '--inspect-preload', 'CONTROL_ARGV')
        result = ObservationController(argv[1],argv[2]).run()
        raw = encode(result,RECEIPT_CAP)
        need(os.write(1,raw) == len(raw),'CONTROL_STDOUT')
        return 0 if result['status'] == 'bootstrap_transport_complete_unreviewed' else 1
    except BaseException:
        try: os.write(2,b'CONTROLLER_REFUSED\n')
        except BaseException: pass
        return 1


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
