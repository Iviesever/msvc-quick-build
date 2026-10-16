"""Explicit isolated preload evidence, NOT an allowlist or execution admission.

Only own modules/maps and their bounded file identities are observed. Two fixed
roles exercise imports, never the owners, decoders, images or preparation slots.
This observer is __main__; its module set is NOT an exact formal bootstrap set.
"""
from pathlib import Path
import hashlib
import os
import stat
import sys

if __name__ == '__main__':
    if not (sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
            and not sys.flags.optimize):
        raise SystemExit('ISOLATED_EVIDENCE_REQUIRED')
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from bcrypt_admission_contract_017 import encode
from bcrypt_import_boundary_017 import describe, references, MODULE_CAP
from bcrypt_toolchain_017 import Budget, snapshot, collect

MAP_CAP, MAP_ROWS, FILE_COUNT = 65536, 512, 256
PASS_CAP, OUTPUT_CAP = 96*1024*1024, 262144
DECODER = '/usr/bin/x86_64-linux-gnu-objdump'
PRELOAD = {
    'owner': ('bcrypt_fixed_bootstrap_017', 'bcrypt_disassembly_io_014',
              'bcrypt_disassembly_listing_014'),
    'controller': ('bcrypt_observation_controller_017',),
}


class EvidenceError(ValueError):
    pass


def need(value, reason):
    if not value:
        raise EvidenceError(reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def admitted_path(path, source_root):
    """Limit even discovery reads; this is not an authorizing file allowlist."""
    need(type(path) is str and 1 < len(path) <= 4096 and path.startswith('/') and
         all(32 <= ord(c) < 127 for c in path), 'PATH_TEXT')
    need(os.path.normpath(path) == path and not path.startswith('//'), 'PATH_SHAPE')
    roots = (str(source_root) + '/', '/usr/lib/', '/usr/local/lib/', '/lib/',
             '/lib64/', '/usr/bin/', '/usr/local/bin/')
    need(path.startswith(roots) or path == '/etc/ld.so.cache', 'PATH_SCOPE')
    return path


def parse_maps(raw):
    """Read mappings' file identities/permissions only; never retain addresses."""
    need(type(raw) is bytes and 0 < len(raw) <= MAP_CAP, 'MAP_BYTES')
    lines = raw.splitlines()
    need(len(lines) <= MAP_ROWS, 'MAP_ROWS')
    files, anonymous = {}, 0
    for line in lines:
        fields = line.split(None, 5)
        need(5 <= len(fields) <= 6 and len(line) <= 8192, 'MAP_ROW')
        span, permissions, offset, device, inode = fields[:5]
        bounds = span.split(b'-'); dev = device.split(b':')
        hextext = lambda s: s and all(c in b'0123456789abcdefABCDEF' for c in s)
        need(len(bounds) == len(dev) == 2 and all(hextext(s) for s in (*bounds, *dev, offset)),
             'MAP_NUMBER')
        need(int(bounds[0], 16) < int(bounds[1], 16) and inode.isdigit() and len(inode) <= 20,
             'MAP_NUMBER')
        need(len(permissions) == 4 and permissions[0] in b'r-' and permissions[1] in b'w-'
             and permissions[2] in b'x-' and permissions[3] in b'ps', 'MAP_PERMISSIONS')
        if len(fields) == 5 or fields[5].startswith(b'['):
            need(inode == b'0', 'MAP_ANONYMOUS'); anonymous += 1; continue
        try:
            path = fields[5].decode('ascii')
        except UnicodeError:
            raise EvidenceError('MAP_PATH') from None
        need(path.startswith('/') and not path.endswith(' (deleted)') and '\\' not in path,
             'MAP_PATH')
        row = dict(device_major=int(dev[0], 16), device_minor=int(dev[1], 16),
                   inode=int(inode), permissions=[])
        need(row['inode'] > 0, 'MAP_INODE')
        if path in files:
            need(all(files[path][k] == row[k] for k in ('device_major', 'device_minor', 'inode')),
                 'MAP_IDENTITY')
        else:
            need(len(files) < FILE_COUNT, 'MAP_FILES'); files[path] = row
        perm = permissions.decode('ascii')
        if perm not in files[path]['permissions']:
            files[path]['permissions'].append(perm)
    for row in files.values(): row['permissions'].sort()
    return dict(files=files, anonymous_regions=anonymous, rows=len(lines))


def own_maps():
    # Raw procfs reads can be short before EOF. A single read is not a snapshot.
    raw = bytearray()
    with open('/proc/self/maps', 'rb', buffering=0) as stream:
        for calls in range(1, 129):
            requested = min(4096, MAP_CAP + 1 - len(raw))
            chunk = stream.read(requested)
            need(type(chunk) is bytes and len(chunk) <= requested, 'MAP_READ')
            if not chunk:
                result = parse_maps(bytes(raw))
                result.update(eof_verified=True, read_calls=calls)
                return result
            raw.extend(chunk)
            need(len(raw) <= MAP_CAP, 'MAP_BYTES')
    raise EvidenceError('MAP_READ_CALLS')


def observed_modules(mapping):
    need(type(mapping) is dict and 0 < len(mapping) <= MODULE_CAP, 'MODULE_COUNT')
    rows, refs, blockers = {}, {}, []
    for name, module in mapping.copy().items():
        need(type(name) is str and 0 < len(name) <= 256, 'MODULE_NAME')
        try:
            row = describe(module)
            rows[name] = {k:list(v) if type(v) is tuple else v for k,v in row.items()}
            refs[name] = references(module)
        except ImportError as error:
            code = error.args[0] if len(error.args) == 1 else 'MODULE_UNREPRESENTABLE'
            need(type(code) is str and code.isascii() and len(code) <= 64, 'MODULE_REASON')
            blockers.append(dict(module=name, reason=code))
            refs[name] = (module,)
    return rows, refs, blockers


def stable_modules(first, current):
    rows, refs, blockers = current
    need(first[0] == rows and first[2] == blockers and first[1].keys() == refs.keys(), 'MODULE_DRIFT')
    need(all(len(first[1][n]) == len(refs[n]) and
             all(a is b for a,b in zip(first[1][n], refs[n])) for n in refs), 'MODULE_DRIFT')


def check_mapped_file(mapping, record):
    state = record['state']
    need(state['ino'] == mapping['inode'] and os.major(state['dev']) == mapping['device_major']
         and os.minor(state['dev']) == mapping['device_minor'], 'MAP_FILE_CHANGED')


def save_new(path, value):
    raw = encode(value, OUTPUT_CAP)
    fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        cursor = 0
        while cursor < len(raw):
            n = os.write(fd, raw[cursor:]); need(n > 0, 'OUTPUT_WRITE'); cursor += n
        os.fsync(fd)
        st = os.fstat(fd)
        named = os.stat(path, follow_symlinks=False)
        need(stat.S_ISREG(st.st_mode) and st.st_nlink == 1 and
             (st.st_dev,st.st_ino) == (named.st_dev,named.st_ino), 'OUTPUT_IDENTITY')
        need(os.pread(fd,len(raw)+1,0) == raw, 'OUTPUT_READBACK')
    finally:
        os.close(fd)


def run(role, out):
    need(role in PRELOAD and sys.platform == 'linux', 'ROLE_PLATFORM')
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
         and not sys.flags.optimize, 'ISOLATION')
    need(not out.exists() and str(out.parent.resolve(strict=True)) == str(out.parent), 'NEW_OUTPUT')
    out.mkdir(mode=0o700)
    try:
        # Fixed imports only. No request/authority generation and no owner invocation.
        for name in PRELOAD[role]: __import__(name)
        from bcrypt_owner_backend_017 import _single_native_thread
        from bcrypt_writer_scope_017 import _policy, _children
        _single_native_thread(); _policy(); need(_children() == [], 'CHILDREN_PRESENT')
        root = Path(__file__).resolve().parent
        # This legacy scanner only reads declared ELF dependencies. It never executes DECODER.
        graph = collect(os.path.realpath(sys.executable), DECODER)
        save_new(out/'static-toolchain.json', graph)
        first = observed_modules(sys.modules)
        before = own_maps()
        candidates, absent = set(before['files']), set()
        candidates.add(os.path.realpath(sys.executable))
        for row in first[0].values():
            for kind in ('file', 'cached', 'origin'):
                value = row[kind]
                if value is None or value in ('built-in', 'frozen'): continue
                admitted_path(value, root)
                if kind == 'cached' and not os.path.lexists(value): absent.add(value)
                else: candidates.add(value)
        need(len(candidates) + len(absent) <= FILE_COUNT, 'FILE_COUNT')
        budget = Budget(seconds=20, limit=PASS_CAP)
        records, aliases = {}, {}
        # Each canonical runtime file is hashed twice; no binary payload is retained.
        for path in sorted(candidates):
            admitted_path(path, root)
            canonical = str(Path(path).resolve(strict=True)); admitted_path(canonical, root)
            if canonical not in records:
                record, _ = snapshot(canonical, budget)
                records[canonical] = record
                need(sum(r['bytes'] for r in records.values()) <= PASS_CAP, 'PASS_CAP')
            aliases[path] = canonical
            if path in before['files']: check_mapped_file(before['files'][path], records[canonical])
        recheck = Budget(seconds=20, limit=budget.content_bytes)
        recheck.deadline = budget.deadline  # Same runtime deadline, not an extra success period.
        for path, prior in records.items():
            current, _ = snapshot(path, recheck)
            need(current == prior, 'FILE_DRIFT')
        for path, canonical in aliases.items():
            need(str(Path(path).resolve(strict=True)) == canonical, 'ALIAS_DRIFT')
        need(all(not os.path.lexists(path) for path in absent), 'CACHED_PATH_DRIFT')
        stable_modules(first, observed_modules(sys.modules))
        after = own_maps()
        # Heap/stack extents may change. File-backed mappings must have the same identities and permissions.
        need(before['files'] == after['files'], 'MAPPING_DRIFT')
        _single_native_thread(); _policy(); need(_children() == [], 'CHILDREN_PRESENT')
        roots = graph['roots']; python = next(r for r in graph['nodes'] if r['path'] == roots['python'])
        interp = python['elf']['interpreter']
        loader = os.path.realpath(interp) if interp else None
        loader_mapped = loader in {aliases[path] for path in before['files']}
        save_new(out/'modules.json', dict(role=role, observed=first[0], blockers=first[2],
                 generated_allowlist=False, formal_bootstrap_module_set=False))
        save_new(out/'runtime-files.json', dict(files=records, aliases=aliases,
                 absent_cached=sorted(absent), mapped_files=before['files'],
                 reads=dict(bytes=budget.content_bytes+recheck.content_bytes,
                            passes=budget.file_passes+recheck.file_passes,
                            eof=budget.eof_requests+recheck.eof_requests),
                 raw_addresses_retained=False))
        summary = dict(schema='bcrypt-runtime-evidence-017-v1', role=role,
            status='EVIDENCE_RECORDED_NOT_QUALIFIED', python_path=os.path.realpath(sys.executable),
            python_version=sys.version, modules=len(first[0]), module_blockers=first[2],
            mapped_files=len(before['files']), runtime_files=len(records),
            runtime_bytes=sum(r['bytes'] for r in records.values()),
            python_pt_interp_mapped=loader_mapped, static_toolchain_nodes=graph['node_count'],
            static_hooks_absent=graph['hooks_absent'],
            probe_module_set_stable=True, file_mappings_stable=True,
            mapping_eof_verified=before['eof_verified'] and after['eof_verified'],
            mapping_read_calls=[before['read_calls'], after['read_calls']],
            own_task_single=True, own_children_empty=True,
            decoder_runtime_observed=False, writer_domain_qualified=False,
            python_formal_closure_qualified=False, execution_admitted=False,
            formal_preparations=0, formal_observations=0, decoder_processes=0,
            original_image_reads=0, source_independently_reviewed=False)
        save_new(out/'summary.json', summary)
        stable_modules(first, observed_modules(sys.modules))
        print('EVIDENCE_RECORDED_NOT_QUALIFIED: '+role)
        return summary
    except BaseException:
        # Partial original records remain. Do not emit exception text or unrelated paths.
        save_new(out/'refused.json', dict(status='EVIDENCE_INCOMPLETE', execution_admitted=False))
        raise


if __name__ == '__main__':
    if len(sys.argv) != 3: raise SystemExit('Usage: SCRIPT owner|controller NEW_OUTPUT')
    try:
        run(sys.argv[1], Path(sys.argv[2]).absolute())
    except BaseException:
        print('RUNTIME_EVIDENCE_REFUSED', file=sys.stderr)
        raise SystemExit(1)
