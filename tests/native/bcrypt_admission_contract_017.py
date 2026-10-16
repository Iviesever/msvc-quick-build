"""Finite 017 request/evidence joins; trusted out-of-band root, not signatures.

The expected request digest must come from the authorized controller, NOT from
hashing an untrusted incoming request. Pinned reviewer/preparer receipts are
assertions by that controller's trusted producers; this code cannot establish
human independence or actual loader behavior from self-declared JSON fields.
"""
from __future__ import annotations
import hashlib
import json

import bcrypt_preflight_017 as provenance
from bcrypt_import_boundary_017 import manifest as module_manifest
from bcrypt_owner_backend_017 import ENV, absolute, sha256_text
from bcrypt_guarded_owner_017 import Refused, need

REQUEST_CAP, RECEIPT_CAP, REPORT_CAP = 262144, 32768, 65536
FILE_CAP, PASS_CAP, FILE_COUNT = 64*1024*1024, 96*1024*1024, 384
LIMITS = dict(identity_passes=2, identity_pass_bytes=PASS_CAP, identity_seconds=4,
              owner_seconds=12, cleanup_seconds=1, outer_seconds=16, child_cpu_seconds=2,
              child_address_space_bytes=268435456, capture_bytes=131072,
              child_open_files=64, child_core_bytes=0, report_bytes=REPORT_CAP)
SOURCES = (
    'bcrypt_preflight_017', 'bcrypt_import_boundary_017',
    'bcrypt_guarded_owner_017', 'bcrypt_owner_backend_017',
    'bcrypt_toolchain_017', 'bcrypt_elf_inventory_017',
    'bcrypt_admission_contract_017', 'bcrypt_bound_observation_017',
    'bcrypt_completion_receipt_017', 'bcrypt_disassembly_io_014',
    'bcrypt_disassembly_model_014', 'bcrypt_disassembly_listing_014',
)
CHECKS = ['source_and_dependencies', 'python_module_closure', 'runtime_loader',
          'resource_and_persistence', 'single_threaded_owner', 'writer_lifetimes']


def fingerprint(raw):
    need(type(raw) is bytes, 'BINDING_BYTES')
    return dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def encode(value, cap=REQUEST_CAP):
    """Bound the object BEFORE encoding; shared canonical wire format, ASCII+LF."""
    need(type(cap) is int and 0 < cap <= REQUEST_CAP, 'JSON_CAP')
    nodes, size, active = 0, 1, set()
    def walk(v, depth):
        nonlocal nodes, size
        nodes += 1
        need(nodes <= 32768 and depth <= 16, 'JSON_GRAPH')
        t = type(v)
        if t is dict or t is list:
            need(len(v) <= 32768 and id(v) not in active, 'JSON_GRAPH')
            active.add(id(v)); size += 2 + max(0, len(v)-1) + (len(v) if t is dict else 0)
            need(size <= cap, 'JSON_CAP')
            if t is dict:
                for k, item in v.items():
                    need(type(k) is str, 'JSON_KEY'); walk(k, depth+1); walk(item, depth+1)
            else:
                for item in v: walk(item, depth+1)
            active.remove(id(v))
        elif t is str:
            need(len(v) <= cap, 'JSON_CAP'); size += 2
            for c in v:
                n=ord(c); need(not 0xD800 <= n <= 0xDFFF, 'JSON_UNICODE')
                size += 2 if c in '\\"\b\f\n\r\t' else 1 if 32 <= n < 127 else 6 if n <= 65535 else 12
                need(size <= cap, 'JSON_CAP')
        elif t is int:
            need(-(1<<63) <= v < (1<<64), 'JSON_NUMBER'); size += len(str(v))
        elif v is None or t is bool: size += 5 if v is False else 4
        else: raise Refused('JSON_TYPE')
        need(size <= cap, 'JSON_CAP')
    walk(value, 0)
    raw=(json.dumps(value, ensure_ascii=True, sort_keys=True, allow_nan=False,
                    separators=(',', ':'))+'\n').encode('ascii')
    need(len(raw) == size, 'JSON_SIZE')
    return raw


def decode(raw, cap=REQUEST_CAP):
    """Bound raw bytes, nesting and token visits before the standard decoder."""
    need(type(cap) is int and 0 < cap <= REQUEST_CAP and type(raw) is bytes
         and 0 < len(raw) <= cap, 'JSON_CAP')
    depth=nodes=0; quoted=escaped=token=False
    for c in raw:
        if quoted:
            if escaped: escaped=False
            elif c == 92: escaped=True
            elif c == 34: quoted=False
        elif c == 34: quoted=True; token=False; nodes+=1
        elif c in (91,123): depth+=1; nodes+=1; token=False
        elif c in (93,125): depth-=1; token=False
        elif c in b' \t\n\r,:': token=False
        elif not token: token=True; nodes+=1
        need(0 <= depth <= 16 and nodes <= 32768, 'JSON_GRAPH')
    need(not quoted and depth == 0, 'JSON_SYNTAX')
    def pairs(items):
        result={}
        for k,v in items:
            need(k not in result, 'JSON_DUPLICATE'); result[k]=v
        return result
    def no_number(_): raise Refused('JSON_NUMBER')
    def integer(s):
        need(len(s) <= 20, 'JSON_NUMBER'); n=int(s)
        need(-(1<<63) <= n < (1<<64), 'JSON_NUMBER'); return n
    try:
        result=json.loads(raw.decode('ascii'), object_pairs_hook=pairs,
                          parse_int=integer, parse_float=no_number, parse_constant=no_number)
    except (ValueError, RecursionError): raise Refused('JSON_SYNTAX') from None
    need(encode(result, cap) == raw, 'JSON_CANONICAL')
    return result


def fields(value, names):
    need(type(value) is dict and value.keys() == set(names), 'BINDING_FIELDS')


def equal(value, expected):
    # Canonical serialization distinguishes False from 0 at every depth.
    need(encode(value) == encode(expected), 'BINDING_MISMATCH')


def label(value):
    need(type(value) is str and 1 <= len(value) <= 80 and
         all(c in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-' for c in value),
         'BINDING_LABEL')


def identity(record, cap=FILE_CAP):
    fields(record, ('path','bytes','sha256')); absolute(record['path']); sha256_text(record['sha256'])
    need(type(record['bytes']) is int and 0 < record['bytes'] <= cap, 'BINDING_SIZE')
    return record


def _runtime_identity(record):
    """Zero content is not absence. Only runtime files can reach this branch.

    Package/source association is checked after the complete module/alias schema.
    Programs, metadata and receipts still use the nonempty identity contract.
    """
    fields(record, ('path','bytes','sha256'))
    if type(record['bytes']) is int and record['bytes'] == 0:
        absolute(record['path'])
        equal(record['sha256'], fingerprint(b'')['sha256'])
    else:
        identity(record)


def _empty_package_paths(modules, aliases):
    paths = set()
    for name, row in modules.items():
        path = row['file']
        if (name not in SOURCES and name != '__main__' and type(path) is str and
                path.endswith('/__init__.py') and row['origin'] == path and
                row['spec_name'] == row['package'] == name and
                row['search'] == row['path'] == (path.rsplit('/', 1)[0],)):
            resolved = aliases.get(path, path)
            if resolved.endswith('/__init__.py'):
                paths.add(resolved)
    return paths


def target(sha):
    sha256_text(sha)
    return dict(runtime_function_index=302, section_index=0, file_start=0x1117C,
                file_end=0x111E3, rva_start=0x1117C, rva_end=0x111E3,
                preferred_vma_start=0x18001117C, sha256=sha)


def validate_request(raw, expected_sha):
    """No input file is opened here. Return an owned, fully bounded request."""
    sha256_text(expected_sha)
    need(type(raw) is bytes and 0 < len(raw) <= REQUEST_CAP, 'BINDING_SIZE')
    need(fingerprint(raw)['sha256'] == expected_sha, 'BINDING_ROOT')
    request=decode(raw)
    fields(request, ('schema','context','receipts'))
    equal(request['schema'], 'bcrypt-bound-request-017-v1')
    c=request['context']
    fields(c, ('call_id','source_commit','output_dir','target','metadata','files',
               'programs','modules','aliases','absent_paths','environment','limits'))
    equal(c['call_id'], '017-static-1')
    commit=c['source_commit']
    need(type(commit) is str and len(commit) == 40 and
         all(ch in '0123456789abcdef' for ch in commit), 'BINDING_COMMIT')
    absolute(c['output_dir']); fields(c['target'],target('0'*64))
    equal(c['target'], target(c['target']['sha256']))
    equal(c['environment'], ENV); equal(c['limits'], LIMITS)
    fields(c['metadata'], provenance.PINS)
    for role, ref in c['metadata'].items():
        identity(ref, 327680)
        equal([ref['bytes'],ref['sha256']], list(provenance.PINS[role]))
    files=c['files']
    need(type(files) is dict and 1 <= len(files) <= FILE_COUNT, 'BINDING_FILES')
    for name, ref in files.items(): label(name); _runtime_identity(ref)
    fields(c['programs'], ('python','decoder','loader','controller'))
    paths={ref['path']:ref for ref in files.values()}
    need(len(paths) == len(files), 'BINDING_DUPLICATE_PATH')
    for path in c['programs'].values():
        absolute(path); need(path in paths, 'BINDING_PROGRAM'); identity(paths[path])
    modules=module_manifest(c['modules'])
    for name in SOURCES:
        need(name in modules and modules[name]['file'] in paths and
             modules[name]['file'].endswith('/'+name+'.py'), 'BINDING_SOURCE')
        identity(paths[modules[name]['file']])
    need(type(c['aliases']) is list and len(c['aliases']) <= 256 and
         type(c['absent_paths']) is list and len(c['absent_paths']) <= 256, 'BINDING_PATHS')
    aliases={}
    for a in c['aliases']:
        fields(a,('path','target')); absolute(a['path']); absolute(a['target'])
        need(a['path'] not in paths and a['path'] not in aliases and a['target'] in paths,
             'BINDING_ALIAS'); aliases[a['path']]=a['target']
    empty_packages = _empty_package_paths(modules, aliases)
    for ref in files.values():
        need(ref['bytes'] > 0 or ref['path'] in empty_packages, 'BINDING_EMPTY_PACKAGE')
    absent=c['absent_paths']
    for path in absent: absolute(path)
    need(len(set(absent)) == len(absent) and not (set(absent) & (paths.keys() | aliases.keys())),
         'BINDING_ABSENCE')
    for name, m in modules.items():
        for kind in ('origin','file','cached'):
            path=m[kind]
            if path is None or (kind == 'origin' and path in ('built-in','frozen')): continue
            need(path in paths or path in aliases or (kind == 'cached' and path in absent),
                 'BINDING_MODULE_BYTES')
            if (kind == 'cached' or name == '__main__') and aliases.get(path, path) in paths:
                identity(paths[aliases.get(path, path)])
    fields(request['receipts'], ('primary','independent','qualification'))
    for ref in request['receipts'].values(): identity(ref, RECEIPT_CAP)
    refs=[*files.values(), *c['metadata'].values(), *request['receipts'].values()]
    all_paths=[r['path'] for r in refs]
    need(len(refs) <= FILE_COUNT and len(set(all_paths)) == len(refs), 'BINDING_DUPLICATE_PATH')
    need(not (set(all_paths)-paths.keys()) & (aliases.keys() | set(absent)), 'BINDING_PATHS')
    need(sum(r['bytes'] for r in refs) <= PASS_CAP, 'BINDING_TOTAL')
    out=c['output_dir']
    need(all(p != out and not p.startswith(out+'/') and not out.startswith(p+'/')
             for p in [*all_paths,*aliases,*absent]), 'BINDING_OUTPUT_OVERLAP')
    return request


def join_evidence(request, metadata, receipts):
    """Hash ALL detached inputs before semantic parsing; no target/image reads."""
    fields(metadata, provenance.PINS); fields(receipts, request['receipts'])
    for refs, supplied in ((request['context']['metadata'],metadata),(request['receipts'],receipts)):
        for name, ref in refs.items():
            raw=supplied[name]
            need(type(raw) is bytes and len(raw) == ref['bytes'], 'BINDING_SIZE')
            equal(fingerprint(raw), {k:ref[k] for k in ('bytes','sha256')})
    try: result=provenance.preflight(metadata)
    except Exception: raise Refused('BINDING_METADATA') from None
    c=request['context']; context_sha=fingerprint(encode(c))['sha256']
    people=[]; records=[]
    for role in ('primary','independent'):
        p=decode(receipts[role], RECEIPT_CAP)
        fields(p, ('schema','role','context_sha256','producer','attempt_id','image',
                   'target','metadata','snapshot_checks','complete_image_passes'))
        equal(p['schema'],'bcrypt-preparation-receipt-017-v1'); equal(p['role'],role)
        equal(p['context_sha256'],context_sha); label(p['producer']); label(p['attempt_id'])
        equal(p['image'],dict(bytes=provenance.IMAGE_BYTES,sha256=provenance.IMAGE_SHA256))
        equal(p['target'],c['target']); equal(p['complete_image_passes'],1)
        equal(p['metadata'],{n:dict(bytes=v[0],sha256=v[1]) for n,v in provenance.PINS.items()})
        equal(p['snapshot_checks'],result['required_future_snapshot_checks'])
        people.append(p['producer']); records.append(p['attempt_id'])
    need(len(set(people)) == len(set(records)) == 2, 'BINDING_PREPARATION_REUSE')
    q=decode(receipts['qualification'], RECEIPT_CAP)
    fields(q, ('schema','context_sha256','author','reviewer','preparations','checks','attempts','decision'))
    equal(q['schema'],'bcrypt-execution-qualification-017-v1')
    equal(q['context_sha256'],context_sha); label(q['author']); label(q['reviewer'])
    need(q['author'] != q['reviewer'] and q['reviewer'] not in people, 'BINDING_REVIEWER')
    equal(q['preparations'],{r:fingerprint(receipts[r]) for r in ('primary','independent')})
    equal(q['checks'],CHECKS); equal(q['attempts'],1)
    equal(q['decision'],'APPROVE_ONE_STATIC_OBSERVATION')
    return dict(context_sha256=context_sha, call_id=c['call_id'], source_commit=c['source_commit'])
