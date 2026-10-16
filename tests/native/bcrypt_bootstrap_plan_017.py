"""Fixed bootstrap inputs and externally authenticated writer-domain premises.

No discovery or authorization generator. The root digest comes from the trusted
registration, never from an arbitrary submitted plan. A pinned authority is an
external review statement, not a kernel observation or a cryptographic signature.
"""
from __future__ import annotations
import os
from pathlib import Path
import time

from bcrypt_admission_contract_017 import (REQUEST_CAP, RECEIPT_CAP, encode, decode,
    fields, equal, fingerprint, identity, join_evidence, validate_request)
from bcrypt_guarded_owner_017 import Refused, need, prepared_target
from bcrypt_owner_backend_017 import absolute, sha256_text
from bcrypt_scoped_transport_017 import _read_directory
from bcrypt_toolchain_017 import Budget, snapshot

PLAN_CAP, LOAD_CAP, LOAD_SECONDS = 16384, 1048576, 2
BOOTSTRAP_NAME = 'bcrypt_fixed_bootstrap_017.py'
CONTROLLER_NAME = 'bcrypt_observation_controller_017.py'
PREMISES = ['dedicated_single_supervisor', 'exclusive_local_output_namespace',
            'no_non_descendant_file_writers', 'no_descriptor_export',
            'no_external_process_mutation']


def disjoint(paths):
    for p in paths: absolute(p)
    need(len(set(paths)) == len(paths), 'BOOT_PATH_OVERLAP')
    for i, left in enumerate(paths):
        for right in paths[i+1:]:
            need(not left.startswith(right+'/') and not right.startswith(left+'/'),
                 'BOOT_PATH_OVERLAP')


class InputReader:
    """Finite declared reads only; no image selection or path search."""
    def __init__(self):
        self.bytes = self.files = 0
        self.start = time.monotonic_ns()

    def read(self, path, cap, expected=None):
        absolute(path)
        need(type(cap) is int and 0 < cap <= 327680 and
             self.bytes + cap <= LOAD_CAP, 'BOOT_READ_BUDGET')
        self.checkpoint()
        p = Path(path)
        try:
            raw = _read_directory(str(p.parent), [p.name], {p.name:cap}, False)[p.name]
        except OSError:
            raise Refused('BOOT_INPUT_IO') from None
        self.bytes += len(raw); self.files += 1
        if expected is not None:
            equal(fingerprint(raw), {k:expected[k] for k in ('bytes','sha256')})
        self.checkpoint()
        return raw

    def checkpoint(self):
        need(0 <= time.monotonic_ns()-self.start < LOAD_SECONDS*1_000_000_000,
             'BOOT_LOAD_DEADLINE')


class BootstrapInputs:
    """One immutable path-rooted request; callbacks, argv and alternate pins absent.

    Reading the two derived inputs consumes no *new preparation*: they must be
    separately prepared, admitted and frozen before this execution registration.
    Rechecks detect observed change; they do not create atomic filesystem locks.
    """
    def __init__(self, plan_path, expected_plan_sha):
        absolute(plan_path); sha256_text(expected_plan_sha)
        self.plan_path, self.plan_sha = plan_path, expected_plan_sha
        self.reader = InputReader()
        self.plan_raw = self.reader.read(plan_path, PLAN_CAP)
        need(fingerprint(self.plan_raw)['sha256'] == expected_plan_sha, 'BOOT_ROOT')
        p = decode(self.plan_raw, PLAN_CAP)
        fields(p, ('schema','request','primary','independent','authority','bootstrap',
                   'transport_dir','receipt_dir'))
        equal(p['schema'], 'bcrypt-bootstrap-plan-017-v1')
        for role, cap in [('request',REQUEST_CAP),('primary',103),('independent',103),
                          ('authority',RECEIPT_CAP),('bootstrap',32768)]:
            identity(p[role],cap)
        self.plan = p
        self.request_raw = self.reader.read(p['request']['path'],p['request']['bytes'],p['request'])
        self.request = validate_request(self.request_raw,p['request']['sha256'])
        c = self.request['context']
        outputs = [c['output_dir'],p['transport_dir'],p['receipt_dir']]
        disjoint(outputs)
        # New input paths cannot alias old identities/absence declarations or outputs.
        extra = [plan_path, *(p[r]['path'] for r in ('request','primary','independent','authority'))]
        declared = [r['path'] for r in [*c['files'].values(),*c['metadata'].values(),
                                       *self.request['receipts'].values()]]
        declared += [a['path'] for a in c['aliases']] + c['absent_paths']
        disjoint([*extra,*declared,*outputs])
        files_by_path = {ref['path']:ref for ref in c['files'].values()}
        self.controller = files_by_path[c['programs']['controller']]
        equal(files_by_path.get(p['bootstrap']['path']),p['bootstrap'])
        need(Path(p['bootstrap']['path']).name == BOOTSTRAP_NAME and
             Path(self.controller['path']).name == CONTROLLER_NAME, 'BOOT_ENTRY')
        # These additions must be in the qualified byte inventory, not guessed at runtime.
        for name in ('bcrypt_bootstrap_plan_017','bcrypt_bound_observation_017'):
            mod = c['modules'].get(name)
            need(type(mod) is dict and mod['file'] in files_by_path and
                 Path(mod['file']).name == name+'.py', 'BOOT_MODULE')
        main = c['modules'].get('__main__')
        need(type(main) is dict and main['file'] == p['bootstrap']['path'], 'BOOT_MAIN')
        self.reader.read(p['bootstrap']['path'],p['bootstrap']['bytes'],p['bootstrap'])
        self.metadata = {r:self.reader.read(v['path'],v['bytes'],v) for r,v in c['metadata'].items()}
        self.receipts = {r:self.reader.read(v['path'],v['bytes'],v) for r,v in self.request['receipts'].items()}
        self.binding = join_evidence(self.request,self.metadata,self.receipts)
        self.binding['request_sha256'] = p['request']['sha256']
        self.authority_raw = self.reader.read(p['authority']['path'],p['authority']['bytes'],p['authority'])
        a = decode(self.authority_raw,RECEIPT_CAP)
        q = decode(self.receipts['qualification'],RECEIPT_CAP)
        equal(a, dict(schema='bcrypt-writer-authority-017-v1',request_sha256=p['request']['sha256'],
              qualification_sha256=fingerprint(self.receipts['qualification'])['sha256'],
              reviewer=q['reviewer'],outputs=outputs,premises=PREMISES,
              decision='QUALIFIED_EXCLUSIVE_WRITER_DOMAIN_ONE_ATTEMPT'))
        self.primary = self.reader.read(p['primary']['path'],103,p['primary'])
        self.independent = self.reader.read(p['independent']['path'],103,p['independent'])
        prepared_target(self.primary,self.independent,c['target']['sha256'])
        self.reader.checkpoint()

    def launch_spec(self):
        c,p = self.request['context'],self.plan
        # Fixed script entry, no -c/-m, user code strings or alternate launch adapter.
        return dict(schema='bcrypt-owner-launch-017-v1',request_sha256=p['request']['sha256'],
            argv=[c['programs']['python'],'-I','-S','-B',p['bootstrap']['path'],self.plan_path,self.plan_sha],
            cwd=str(Path(p['bootstrap']['path']).parent),output_dir=p['transport_dir'],
            environment=c['environment'],wall_ms=14000)

    def verify_control_sources(self):
        """Check explicit control-program bytes without importing a caller's path."""
        r = InputReader()
        c = self.request['context']; lookup = {v['path']:v for v in c['files'].values()}
        for path in (self.plan['bootstrap']['path'],self.controller['path'],
                     c['modules']['bcrypt_bootstrap_plan_017']['file']):
            ref = lookup[path]
            need(ref['bytes'] <= 32768, 'BOOT_SOURCE_CAP')
            r.read(path,ref['bytes'],ref)
        return dict(files=r.files,bytes=r.bytes)

    def verify_launch_files(self):
        """One explicitly budgeted runtime-byte pass before/after the owner process.

        Verification inside the owner cannot substitute for checking the
        interpreter and its declared dependencies BEFORE starting that owner.
        No inventory discovery, loader execution or native target read occurs.
        """
        c = self.request['context']; refs = list(c['files'].values())
        budget = Budget(seconds=4,limit=sum(r['bytes'] for r in refs))
        def paths():
            for alias in c['aliases']:
                budget.checkpoint()
                need(str(Path(alias['path']).resolve(strict=True)) == alias['target'], 'BOOT_ALIAS')
            for path in c['absent_paths']:
                budget.checkpoint(); need(not os.path.lexists(path), 'BOOT_ABSENCE')
        equal(dict(os.environ),c['environment']); paths()
        for ref in refs:
            budget.checkpoint()
            need(os.lstat(ref['path']).st_size == ref['bytes'], 'BOOT_RUNTIME_BYTES')
            observed,_ = snapshot(ref['path'],budget)
            equal({k:observed[k] for k in ('path','bytes','sha256')},ref)
        paths(); equal(dict(os.environ),c['environment']); budget.checkpoint()
        need(budget.file_passes == len(refs) and budget.content_bytes == sum(r['bytes'] for r in refs),
             'BOOT_READ_ACCOUNTING')
        return dict(files=budget.file_passes,bytes=budget.content_bytes,eof_requests=budget.eof_requests)

    def recheck(self):
        """One bounded detached-input recheck, no decoder or new launch."""
        current = BootstrapInputs(self.plan_path,self.plan_sha)
        equal(current.plan,self.plan)
        equal(current.binding,self.binding)
        return current
