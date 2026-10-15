"""Bind the prepared 017 owner to an authenticated request and two identity passes.

No discovery, image selection, decoder CLI or permission grant is performed by
construction. Run only in the separately qualified dedicated owner interpreter.
A trusted external controller still reserves the invocation and records its exit.
"""
from __future__ import annotations
import os
from pathlib import Path
import sys
import threading
from types import MethodType

from bcrypt_admission_contract_017 import (LIMITS, REPORT_CAP, encode, equal,
    fingerprint, join_evidence, validate_request)
from bcrypt_guarded_owner_017 import GuardedPreparedOwner, Refused, need, prepared_target
from bcrypt_owner_backend_017 import PreparedNativeBackend
from bcrypt_toolchain_017 import Budget, snapshot

_BACKEND_METHODS = {name: getattr(PreparedNativeBackend, name) for name in
                    ('reserve', 'write', 'launch', 'parse', 'candidate', 'close', 'target_spec')}


def _dependency(module, symbol):
    # No import or module __getattr__ at this point: all paths must be preloaded.
    loaded=sys.modules.get(module)
    need(loaded is not None and symbol in loaded.__dict__, 'BINDING_DEPENDENCY')
    return loaded.__dict__[symbol]


class BoundObservation:
    """Single-use request join and owner adapter, not a new generic callback gate.

    Constructor inputs are already bounded immutable bytes supplied by the outer
    controller. Public code has no alternative metadata pins or synthetic mode.
    Tests patch metadata provenance and process dependencies locally, explicitly.
    A caller-supplied empty verify callback is no longer accepted by this entry.
    """
    def __init__(self, request_raw, expected_sha, *, metadata, receipts):
        self._request=validate_request(request_raw, expected_sha)
        self._binding=join_evidence(self._request, metadata, receipts)
        self._binding['request_sha256']=expected_sha
        self._used=False
        self._phase=0
        self._backend=None
        self._dependencies=()
        self._verification={'before':None, 'after':None, 'failure':None}
        # Retain no caller-owned mutable dict or detached metadata buffer.

    def _envelope(self, report):
        value=dict(report, binding=dict(self._binding), verification=dict(self._verification))
        return value

    def _check_backend(self):
        backend=self._backend
        c=self._request['context']
        need(type(backend) is PreparedNativeBackend, 'BINDING_BACKEND')
        equal([backend.output_path,backend.tool_path,backend.target_sha,backend.request_sha],
              [c['output_dir'],c['programs']['decoder'],c['target']['sha256'],self._binding['request_sha256']])
        for name, function in _BACKEND_METHODS.items():
            method=getattr(backend,name)
            need(type(method) is MethodType and method.__self__ is backend and
                 method.__func__ is function, 'BINDING_BACKEND_METHOD')
        factory,parser=self._dependencies
        need(backend.encoder is encode and backend.factory is factory and backend.parser is parser and
             _dependency('bcrypt_disassembly_io_014','OutputDirectory') is factory and
             _dependency('bcrypt_disassembly_listing_014','parse_listing') is parser,
             'BINDING_DEPENDENCY')

    def _verify(self, phase):
        try:
            need(self._verification['failure'] is None and self._phase < 2 and
                 phase == ('before','after')[self._phase], 'BINDING_PHASE')
            self._phase += 1  # Failure consumes the pass; no implicit retry.
            self._check_backend()
            c=self._request['context']
            need(threading.active_count() == 1, 'BINDING_THREADS')
            need(os.path.realpath(sys.executable) == c['programs']['python'], 'BINDING_PYTHON')
            equal(dict(os.environ), c['environment'])
            refs=[*c['files'].values(),*c['metadata'].values(),*self._request['receipts'].values()]
            total=sum(r['bytes'] for r in refs)
            budget=Budget(seconds=LIMITS['identity_seconds'],limit=total)
            def paths():
                for a in c['aliases']:
                    budget.checkpoint()
                    need(str(Path(a['path']).resolve(strict=True)) == a['target'], 'BINDING_ALIAS')
                for path in c['absent_paths']:
                    budget.checkpoint(); need(not os.path.lexists(path), 'BINDING_ABSENCE')
            paths()
            for ref in refs:
                budget.checkpoint()
                need(os.lstat(ref['path']).st_size == ref['bytes'], 'BINDING_SIZE')
                observed,_=snapshot(ref['path'], budget)
                equal({k:observed[k] for k in ('path','bytes','sha256')},ref)
            paths(); budget.checkpoint()
            self._check_backend()
            # Recheck non-file inputs after the entire pass as well.
            equal(dict(os.environ), c['environment'])
            need(os.path.realpath(sys.executable) == c['programs']['python'] and
                 threading.active_count() == 1, 'BINDING_RUNTIME')
            need(budget.content_bytes == total and budget.file_passes == len(refs), 'BINDING_READS')
            self._verification[phase]=dict(files=budget.file_passes, bytes=budget.content_bytes,
                                            eof_requests=budget.eof_requests)
        except (KeyboardInterrupt, SystemExit):
            self._verification['failure']='INTERRUPTED'
            raise
        except BaseException:
            self._verification['failure']='IDENTITY_PASS_REFUSED'
            raise Refused('PREPARATION') from None

    def run(self, backend, primary, independent):
        need(not self._used, 'REUSED'); self._used=True
        c=self._request['context']
        need(type(backend) is PreparedNativeBackend, 'BINDING_BACKEND')
        self._backend=backend
        self._dependencies=(_dependency('bcrypt_disassembly_io_014','OutputDirectory'),
                            _dependency('bcrypt_disassembly_listing_014','parse_listing'))
        self._check_backend()
        need(not backend.used and not backend.closed and not backend.launch_used and
             backend.directory is None and not backend.captures_open and not backend.expected,
             'BINDING_BACKEND_USED')
        prepared_target(primary,independent,c['target']['sha256'])
        owner=GuardedPreparedOwner(c['modules'])
        binding=self
        class Sink:
            def reserve(self): return backend.reserve()
            def write(self,name,raw): return backend.write(name,raw)
            def candidate(self,report): return backend.candidate(binding._envelope(report))
            def close(self): return backend.close()
        report=owner.run(primary,independent,c['target']['sha256'],sink=Sink(),
                         launch=backend.launch,parse=backend.parse,verify=self._verify)
        # The backend may have drifted during candidate persistence/close, after
        # the second file pass. Never rewrite that already-persisted candidate.
        try:
            self._check_backend()
        except BaseException as error:
            interrupted=isinstance(error,(KeyboardInterrupt,SystemExit))
            code='INTERRUPTED' if interrupted else 'PREPARATION'
            detail='INTERRUPTED' if interrupted else 'BACKEND_BINDING_REFUSED'
            self._verification['failure']=self._verification['failure'] or detail
            report['first_failure']=report['first_failure'] or code
            report['status'],report['instructions']='refused',None
        # Immutable post-close record; clean process exit is still NOT observed.
        return encode(self._envelope(report),REPORT_CAP)
