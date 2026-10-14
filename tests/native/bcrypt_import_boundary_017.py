"""017 post-preload import boundary; no file reads, decoder or admission.

Use only in a dedicated trusted, single-threaded owner after a separately reviewed
preload/identity phase. This is an import-protocol tripwire, NOT a sandbox, a file
hash check or a proof about native dlopen. A caller supplies the expected COMPLETE
module metadata; this module never silently learns an allowlist from live state.
"""
from __future__ import annotations
import sys
from importlib.machinery import ModuleSpec
from types import ModuleType

MODULE_CAP, FINDER_CAP, PATH_CAP = 256, 16, 16
TEXT_CAP, TOTAL_TEXT_CAP = 4096, 131072
FIELDS = frozenset(('origin', 'file', 'cached', 'package', 'spec_name', 'search', 'path'))


class Refused(ImportError):
    """Stable non-sensitive reason, not a serialized module/host inventory."""


def need(ok, code):
    if not ok:
        raise Refused(code)


def text(value):
    need(value is None or (type(value) is str and len(value) <= TEXT_CAP and
         all(32 <= ord(c) < 127 for c in value)), 'MODULE_TEXT')
    return value


def locations(value):
    if value is None:
        return None
    need(type(value) in (list, tuple) and len(value) <= PATH_CAP, 'MODULE_LOCATIONS')
    need(all(type(v) is str and v.startswith('/') for v in value), 'MODULE_LOCATION_PATH')
    return tuple(text(v) for v in value)


def normalize(record):
    need(type(record) is dict and record.keys() == FIELDS, 'MODULE_FIELDS')
    row = {k: locations(record[k]) if k in ('search', 'path') else text(record[k])
           for k in FIELDS}
    for key in ('file', 'cached'):
        need(row[key] is None or row[key].startswith('/'), 'MODULE_FILE_PATH')
    origin = row['origin']
    need(origin in (None, 'built-in', 'frozen') or origin.startswith('/'), 'MODULE_ORIGIN')
    return row


def describe(module):
    """Inspect one plain module in memory, without invoking module __getattr__."""
    need(type(module) is ModuleType, 'MODULE_OBJECT')
    attrs = module.__dict__
    spec = attrs.get('__spec__')
    need(spec is None or type(spec) is ModuleSpec, 'MODULE_SPEC')
    data = {} if spec is None else spec.__dict__
    need(data.get('_initializing', False) is False, 'MODULE_STILL_INITIALIZING')
    need(spec is not None or attrs.get('__name__') == '__main__', 'MODULE_SPEC_MISSING')
    row = normalize(dict(origin=data.get('origin'), file=attrs.get('__file__'),
                         cached=attrs.get('__cached__'), package=attrs.get('__package__'),
                         spec_name=data.get('name'), search=data.get('submodule_search_locations'),
                         path=attrs.get('__path__')))
    need(spec is None or row['origin'] is not None, 'NAMESPACE_MODULE_UNSUPPORTED')
    need(spec is not None or row['file'] is not None, 'MAIN_SOURCE_UNSPECIFIED')
    return row


def manifest(expected):
    need(type(expected) is dict and 1 <= len(expected) <= MODULE_CAP, 'MODULE_COUNT')
    result, size = {}, 0
    for name, record in expected.items():
        need(type(name) is str and 1 <= len(name) <= 256 and
             all(part and all(c.isascii() and (c.isalnum() or c == '_') for c in part)
                 for part in name.split('.')), 'MODULE_NAME')
        row = normalize(record)
        size += len(name)
        for value in row.values():
            size += sum(len(v) for v in value) if type(value) is tuple else len(value or '')
        need(size <= TOTAL_TEXT_CAP, 'MODULE_TOTAL_TEXT')
        result[name] = row
    return result


def references(module):
    attrs = module.__dict__
    spec = attrs.get('__spec__')
    loader = None if spec is None else spec.__dict__.get('loader')
    # Keep strong references, not only id() values susceptible to later reuse.
    return module, spec, loader, attrs.get('__loader__')


class ImportBoundary:
    """One use: activate -> checkpoints -> close; refusal is irreversible.

    runtime defaults to sys. The explicit runtime seam is for in-memory tests;
    passing a fake runtime never qualifies the real interpreter. No context-manager
    method can swallow a body exception. The outer owner retains that exception.
    """
    def __init__(self, expected, *, runtime=sys):
        self.expected = manifest(expected)
        self.runtime, self.phase, self.reason = runtime, 'new', None
        self.module_map, self.pins, self.installed = None, {}, None
        self.finders = ()

    def fail(self, code):
        if self.reason is None:
            self.reason = code
        raise Refused(self.reason)

    def _modules(self, initial=False):
        mapping = self.runtime.modules
        need(type(mapping) is dict and len(mapping) == len(self.expected) and
             mapping.keys() == self.expected.keys(), 'MODULE_SET_CHANGED')
        if not initial:
            need(mapping is self.module_map, 'MODULE_MAP_REPLACED')
        for name, row in self.expected.items():
            module = mapping[name]
            need(describe(module) == row, 'MODULE_METADATA_CHANGED')
            refs = references(module)
            if initial:
                self.pins[name] = refs
            else:
                need(all(a is b for a, b in zip(refs, self.pins[name])), 'MODULE_IDENTITY_CHANGED')
        if initial:
            self.module_map = mapping

    def activate(self):
        if self.phase != 'new':
            self.fail('BOUNDARY_REUSED')
        self.phase = 'activating'
        try:
            self._modules(initial=True)
            finders = self.runtime.meta_path
            need(type(finders) is list and len(finders) <= FINDER_CAP and
                 all(f is not self for f in finders), 'FINDER_SET')
            self.finders = tuple(finders)
            self.installed = [self, *finders]
            self.runtime.meta_path = self.installed
            self.phase = 'active'
            self.checkpoint()
        except BaseException:
            if self.reason is None:
                self.reason = 'ACTIVATION_FAILED'
            raise
        return self

    def find_spec(self, fullname, path=None, target=None):
        # Always raise rather than return None: otherwise the next finder runs.
        # Includes importlib.reload, even for an already admitted module name.
        self.fail('LATE_IMPORT_OR_RELOAD')

    def checkpoint(self):
        if self.reason is not None:
            raise Refused(self.reason)
        try:
            need(self.phase == 'active', 'BOUNDARY_NOT_ACTIVE')
            current = self.runtime.meta_path
            need(current is self.installed and type(current) is list and
                 len(current) == len(self.finders) + 1 and current[0] is self and
                 all(a is b for a, b in zip(current[1:], self.finders)), 'FINDER_DRIFT')
            self._modules()
        except Refused as exc:
            self.fail(str(exc))
        except BaseException:
            if self.reason is None:
                self.reason = 'CHECK_FAILED'
            raise

    def close(self):
        if self.phase == 'closed':
            self.fail('BOUNDARY_REUSED')
        try:
            self.checkpoint()
        except Refused:
            pass  # Preserve the first reason, never convert refusal into success.
        except BaseException:
            if self.reason is None:
                self.reason = 'CHECK_FAILED'
            raise
        finally:
            detached = False
            current = self.runtime.meta_path
            # Remove ONLY our finder. Do not restore over someone else's edits.
            if type(current) is list and len(current) <= FINDER_CAP + 1:
                current[:] = [item for item in current if item is not self]
                detached = True
            elif self.reason is None:
                self.reason = 'CLEANUP_UNCONFIRMED'
            self.phase = 'closed'
        return dict(schema='bcrypt-import-boundary-017-v1',
                    status='closed_unchanged' if self.reason is None else 'refused',
                    reason=self.reason, finder_detached=detached, module_count=len(self.expected),
                    execution_admitted=False, source_independently_reviewed=False,
                    runtime_loader_verified=False, python_module_bytes_verified=False,
                    python_stdlib_and_dynamic_imports_qualified=False)
