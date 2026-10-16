"""Review an explicit recorded runtime candidate; separately recheck its files.

review_binding has no filesystem reads. verify_current_files reads only the
previously bound paths, never discovers replacements or launches a target. All four
byte roots must be pinned by the caller's external review record before this
functions are called. A matching candidate is NOT a complete/admitted request.
Different process snapshots are joined by content, never by inode or timestamp.
"""
import os
import stat
import sys
from pathlib import Path

from bcrypt_toolchain_017 import Budget, snapshot, state
from bcrypt_owner_backend_017 import ENV, _single_native_thread
from bcrypt_admission_contract_017 import (
    REQUEST_CAP, FILE_COUNT, PASS_CAP, SOURCES, decode, encode, fields, equal,
    fingerprint, identity, label, _runtime_identity, _empty_package_paths)
from bcrypt_bootstrap_plan_017 import BOOTSTRAP_NAME, CONTROLLER_NAME
from bcrypt_import_boundary_017 import manifest
from bcrypt_owner_backend_017 import absolute, sha256_text
from bcrypt_guarded_owner_017 import need, Refused

ROLES = ('owner', 'controller', 'toolchain')
RUNTIME_FIELDS = ('programs', 'modules', 'files', 'aliases', 'absent_paths')


def _pinned(raw, ref):
    fields(ref, ('bytes', 'sha256')); sha256_text(ref['sha256'])
    need(type(ref['bytes']) is int and 0 < ref['bytes'] <= REQUEST_CAP,
         'ENTRY_BIND_SIZE')
    need(type(raw) is bytes and len(raw) == ref['bytes'], 'ENTRY_BIND_SIZE')
    equal(fingerprint(raw), ref)


def _ref(record):
    need(type(record) is dict, 'ENTRY_BIND_FILE')
    ref = {k: record[k] for k in ('path', 'bytes', 'sha256')}
    _runtime_identity(ref)
    return ref


def _entry(value, role):
    equal(value['schema'], 'bcrypt-entry-preload-evidence-017-v1')
    equal(value['status'], 'ENTRY_PRELOAD_RECORDED_NOT_QUALIFIED')
    equal(value['phase'], 'before_request_read'); equal(value['role'], role)
    for name in ('expected_modules_generated', 'execution_admitted',
                 'source_independently_reviewed', 'runtime_loader_verified',
                 'writer_domain_qualified', 'full_execution_closure_verified'):
        equal(value[name], False)
    for name in ('request_reads', 'target_reads', 'decoder_processes', 'formal_observations'):
        equal(value[name], 0)
    modules = manifest(value['modules'])
    main = modules['__main__']; path = value['entry_file']; absolute(path)
    expected = BOOTSTRAP_NAME if role == 'owner' else CONTROLLER_NAME
    need(path.rsplit('/', 1)[-1] == expected and main['file'] == path and
         main['spec_name'] is main['origin'] is main['cached'] is None, 'ENTRY_BIND_MAIN')
    files = value['files']
    need(type(files) is dict and 0 < len(files) <= 256, 'ENTRY_BIND_FILES')
    for path, ref in files.items():
        equal(path, _ref(ref)['path'])
    refs = {path: _ref(ref) for path, ref in files.items()}
    need(value['python_file'] in refs and value['entry_file'] in refs, 'ENTRY_BIND_PROGRAM')
    equal(value['reads'], dict(passes=2*len(refs), eof_requests=2*len(refs),
                              content_bytes=2*sum(r['bytes'] for r in refs.values())))
    return modules, refs


def review_binding(candidate_raw, evidence, expected_roots):
    """Compare a supplied runtime slice; return only counts, digests and limitations.

    expected_roots is out-of-band, not obtained from candidate_raw or evidence.
    This does not authenticate the reviewer, original CI or current host itself.
    The candidate has NO target, receipts, call_id or permission fields, and thus
    cannot be submitted as a bcrypt-bound-request-017-v1 execution request.
    """
    fields(evidence, ROLES); fields(expected_roots, ('candidate', *ROLES))
    # Check all raw bounds/identities before parsing even one document.
    _pinned(candidate_raw, expected_roots['candidate'])
    for role in ROLES: _pinned(evidence[role], expected_roots[role])
    candidate = decode(candidate_raw)
    fields(candidate, ('schema', 'basis', 'runtime'))
    equal(candidate['schema'], 'bcrypt-runtime-binding-candidate-017-v1')
    equal(candidate['basis'], {r: expected_roots[r] for r in ROLES})
    runtime = candidate['runtime']; fields(runtime, RUNTIME_FIELDS)
    values = {role: decode(evidence[role]) for role in ROLES}
    owner, controller, graph = (values[r] for r in ROLES)
    owner_modules, owner_refs = _entry(owner, 'owner')
    controller_modules, controller_refs = _entry(controller, 'controller')
    equal(runtime['modules'], owner['modules'])
    for name in SOURCES:
        need(name in owner_modules, 'ENTRY_BIND_SOURCE')
    # Shared module descriptors must agree; the two actual __main__ objects differ.
    for name in owner_modules.keys() & controller_modules.keys() - {'__main__'}:
        equal(owner['modules'][name], controller['modules'][name])
    equal(owner['python_file'], controller['python_file'])
    equal(owner['python_version'], controller['python_version'])
    equal(graph['schema'], 'bcrypt-static-toolchain-017-v1')
    equal(graph['status'], 'STATIC_TOOLCHAIN_IDENTITY_RECORDED')
    equal(graph['execution_admitted'], False)
    fields(graph['roots'], ('python', 'decoder'))
    equal(graph['roots']['python'], owner['python_file'])
    need(type(graph['nodes']) is list and 1 <= len(graph['nodes']) <= 32 and
         type(graph['edges']) is list and len(graph['edges']) <= 2048, 'ENTRY_BIND_GRAPH')
    equal(graph['node_count'], len(graph['nodes'])); equal(graph['edge_count'], len(graph['edges']))
    nodes = {_ref(n)['path']: n for n in graph['nodes']}
    need(len(nodes) == len(graph['nodes']) and set(graph['roots'].values()) <= nodes.keys(),
         'ENTRY_BIND_GRAPH')
    loaders = set()
    for root in graph['roots'].values():
        links = [e for e in graph['edges'] if e['source'] == root and e['kind'] == 'PT_INTERP']
        need(len(links) == 1 and links[0]['provider'] in nodes, 'ENTRY_BIND_LOADER')
        loaders.add(links[0]['provider'])
    need(len(loaders) == 1, 'ENTRY_BIND_LOADER')
    equal(runtime['programs'], dict(python=owner['python_file'],
        decoder=graph['roots']['decoder'], loader=next(iter(loaders)), controller=controller['entry_file']))

    joined = {}
    for record in [*owner_refs.values(), *controller_refs.values(), *graph['nodes'], graph['cache']]:
        ref = _ref(record); path = ref['path']
        if path in joined: equal(joined[path], ref)
        joined[path] = ref
    need(len(joined) <= FILE_COUNT and sum(r['bytes'] for r in joined.values()) <= PASS_CAP,
         'ENTRY_BIND_BUDGET')
    files = runtime['files']; need(type(files) is dict and 0 < len(files) <= FILE_COUNT, 'ENTRY_BIND_FILES')
    for name, ref in files.items(): label(name); fields(ref, ('path', 'bytes', 'sha256')); _runtime_identity(ref)
    proposed = {r['path']: r for r in files.values()}
    need(len(proposed) == len(files), 'ENTRY_BIND_DUPLICATE')
    equal(proposed, joined)  # No omitted, invented or silently superseded file.

    aliases, absent = {}, set()
    for entry in (owner, controller):
        need(type(entry['aliases']) is dict and len(entry['aliases']) <= 256 and
             type(entry['absent_cached']) is list and len(entry['absent_cached']) <= 256,
             'ENTRY_BIND_PATHS')
        equal(len(set(entry['absent_cached'])), len(entry['absent_cached']))
        absent.update(entry['absent_cached'])
    need(type(graph['aliases']) is list and len(graph['aliases']) <= 256 and
         type(graph['absence_checks']) is list and len(graph['absence_checks']) <= 256,
         'ENTRY_BIND_PATHS')
    for path, target in [*owner['aliases'].items(), *controller['aliases'].items(),
                         *((a['path'], a['target']) for a in graph['aliases'])]:
        absolute(path); absolute(target); need(target in joined, 'ENTRY_BIND_ALIAS')
        if path == target: continue
        need(path not in joined and (path not in aliases or aliases[path] == target), 'ENTRY_BIND_ALIAS')
        aliases[path] = target
    for check in graph['absence_checks']:
        equal(check['absent'], True); absent.add(check['path'])
    for path in absent: absolute(path)
    need(len(aliases) <= 256 and len(absent) <= 256 and
         not absent & (joined.keys() | aliases.keys()), 'ENTRY_BIND_ABSENCE')
    equal(runtime['aliases'], [dict(path=p, target=t) for p,t in sorted(aliases.items())])
    equal(runtime['absent_paths'], sorted(absent))
    for modules in (owner_modules, controller_modules):
        empty = _empty_package_paths(modules, aliases)
        for row in modules.values():
            for kind in ('file', 'origin', 'cached'):
                path = row[kind]
                if path is None or (kind == 'origin' and path in ('built-in', 'frozen')): continue
                if kind == 'cached' and path in absent: continue
                actual = aliases.get(path, path)
                need(actual in joined, 'ENTRY_BIND_MODULE_BYTES')
                if joined[actual]['bytes'] == 0:
                    need(actual in empty and kind != 'cached', 'ENTRY_BIND_EMPTY')
    for path in runtime['programs'].values(): identity(joined[path])
    identity(joined[owner['entry_file']])
    for name in SOURCES: identity(joined[owner_modules[name]['file']])
    return dict(schema='bcrypt-runtime-binding-review-017-v1',
        status='RECORDED_RUNTIME_BINDING_MATCHED_NOT_QUALIFIED',
        candidate_sha256=expected_roots['candidate']['sha256'],
        runtime_sha256=fingerprint(encode(runtime))['sha256'],
        evidence={r: dict(expected_roots[r]) for r in ROLES},
        owner_modules=len(owner_modules), controller_modules=len(controller_modules),
        files=len(joined), file_bytes=sum(r['bytes'] for r in joined.values()),
        aliases=len(aliases), absent_paths=len(absent),
        complete_request=False, current_host_verified=False, snapshots_atomic=False,
        writer_domain_qualified=False, preparations_verified=False,
        independent_review_signed=False, execution_admitted=False, clears_hold=False)


def verify_current_files(candidate_raw, evidence, expected_roots):
    """Revalidate the pinned candidate on this host, without promoting authority.

    Complete the unchanged four-input offline review BEFORE reading any candidate
    path. Check the running interpreter and fixed environment, then read exactly
    two passes of the declared files under one 20-second cooperative budget.
    Compare this host's own per-file states across passes, not historical inodes.
    Stop on the first failure, retain completed read accounting, never substitute
    a new path/digest, and never remove an unexpected file or broken symlink.

    This is a file-identity check, not an actual owner execution, atomic snapshot,
    loader trace, process-writer census or proof of independence. Even a successful
    result cannot be submitted as a formal request or qualification receipt.
    """
    reviewed = review_binding(candidate_raw, evidence, expected_roots)
    runtime = decode(candidate_raw)['runtime']
    refs = runtime['files']; total = sum(r['bytes'] for r in refs.values())
    budget = Budget(seconds=20, limit=2*total)
    result = dict(schema='bcrypt-current-file-binding-017-v1',
        status='CURRENT_RUNTIME_FILES_REFUSED',
        candidate_sha256=reviewed['candidate_sha256'],
        runtime_sha256=reviewed['runtime_sha256'],
        current_files_verified=False, interpreter_identity_checked=False,
        completed_passes=0, last_file=None, failure_stage=None, interrupted=False,
        passes=[], reads=None, snapshots_atomic=False, current_host_qualified=False,
        runtime_module_closure_verified=False, writer_domain_qualified=False,
        preparations_verified=False, independent_review_signed=False,
        execution_admitted=False, clears_hold=False)
    stage = 'runtime'; first = {}

    def runtime_check():
        budget.checkpoint()
        need(str(Path(sys.executable).resolve(strict=True)) == runtime['programs']['python'],
             'ENTRY_HOST_PYTHON')
        equal(dict(os.environ), ENV)
        _single_native_thread()

    def paths():
        nonlocal stage
        stage = 'aliases'
        for item in runtime['aliases']:
            budget.checkpoint()
            need(str(Path(item['path']).resolve(strict=True)) == item['target'], 'ENTRY_HOST_ALIAS')
        stage = 'absence'
        for path in runtime['absent_paths']:
            budget.checkpoint()
            # lexists, not exists: even a dangling symlink violates absence.
            need(not os.path.lexists(path), 'ENTRY_HOST_ABSENCE')

    try:
        runtime_check()
        for index in range(2):
            paths(); stage = 'files'; observed = {}
            for name, ref in refs.items():
                result['last_file'] = name
                budget.checkpoint()
                before = os.lstat(ref['path'])
                need(stat.S_ISREG(before.st_mode) and before.st_size == ref['bytes'], 'ENTRY_HOST_FILE')
                record, _ = snapshot(ref['path'], budget)
                equal({k:record[k] for k in ('path', 'bytes', 'sha256')}, ref)
                equal(record['state'], state(before))
                observed[name] = record['state']
                if index:
                    equal(observed[name], first[name])
            paths(); stage = 'runtime'; runtime_check()
            if not index: first = observed
            result['passes'].append(dict(files=len(observed), content_bytes=total,
                eof_requests=len(observed), states_sha256=fingerprint(encode(observed))['sha256']))
            result['completed_passes'] += 1
        stage = 'accounting'; budget.checkpoint()
        need(budget.file_passes == budget.eof_requests == 2*len(refs) and
             budget.content_bytes == 2*total, 'ENTRY_HOST_READS')
        result['interpreter_identity_checked'] = True
        result['current_files_verified'] = True
        result['status'] = 'CURRENT_RUNTIME_FILES_MATCHED_NOT_QUALIFIED'
    except (KeyboardInterrupt, SystemExit):
        result['failure_stage'], result['interrupted'] = stage, True
    except (OSError, ValueError, Refused):
        # Neither host paths nor arbitrary exception text become public errors.
        result['failure_stage'] = stage
    result['reads'] = dict(passes=budget.file_passes, eof_requests=budget.eof_requests,
                          declared_content_bytes=budget.content_bytes)
    return result
