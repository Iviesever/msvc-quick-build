"""Read-only declared ELF dependency inventory for checkpoint 017.

Never runs a decoder or loader. Static resolution below deliberately rejects
ambiguity; it does not emulate glibc or certify CPython's future dynamic imports.
The caller owns the trusted host path namespace, not an adversarial filesystem.
"""
from __future__ import annotations
from collections import deque
import hashlib
import os
from pathlib import Path
import stat
import time

from bcrypt_elf_inventory_017 import FILE_CAP, Refused, need, parse_elf

DEFAULT_DIRS = ('/usr/local/lib', '/usr/lib/x86_64-linux-gnu',
                '/lib/x86_64-linux-gnu', '/usr/lib', '/lib')
NODE_CAP, EDGE_CAP, TOTAL_CAP, CHUNK = 32, 2048, 192*1024*1024, 65536


class Budget:
    def __init__(self, seconds=20.0, limit=TOTAL_CAP):
        self.deadline = time.monotonic()+seconds
        self.limit, self.content_bytes, self.eof_requests, self.file_passes = limit, 0, 0, 0

    def checkpoint(self):
        need(time.monotonic() < self.deadline, 'cooperative inventory deadline')

    def allocate(self, size):
        self.checkpoint()
        need(type(size) is int and 0 <= size <= self.limit-self.content_bytes, 'aggregate read budget')
        self.content_bytes += size


def state(s):
    return {name: getattr(s, 'st_'+name) for name in
            ('dev', 'ino', 'mode', 'nlink', 'size', 'mtime_ns', 'ctime_ns')}


def canonical(path: str) -> str:
    need(type(path) is str and len(path) <= 4096 and Path(path).is_absolute(), 'absolute bounded host path required')
    need(all(32 <= ord(c) < 127 for c in path), 'host path must be printable ASCII')
    return str(Path(path).resolve(strict=True))


def snapshot(path: str, budget: Budget, *, keep=False):
    budget.checkpoint()
    resolved = canonical(path)
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    fd = os.open(resolved, flags)
    try:
        before = os.fstat(fd)
        need(stat.S_ISREG(before.st_mode) and 0 <= before.st_size <= FILE_CAP, 'ordinary bounded host file required')
        need(before.st_mode & 0o6000 == 0, 'set-id host file not admitted')
        budget.allocate(before.st_size)
        digest, parts, remaining = hashlib.sha256(), bytearray(), before.st_size
        while remaining:
            budget.checkpoint()
            chunk = os.read(fd, min(remaining, CHUNK))
            need(bool(chunk), 'short host file read')
            remaining -= len(chunk); digest.update(chunk)
            if keep: parts.extend(chunk)
        budget.checkpoint()
        budget.eof_requests += 1
        need(os.read(fd, 1) == b'', 'host file grew')
        after = os.fstat(fd)
        need(state(before) == state(after), 'host file identity changed while reading')
        need(canonical(path) == resolved, 'host alias changed while reading')
        path_state = os.stat(resolved, follow_symlinks=False)
        need(state(path_state) == state(after), 'host path no longer names read file')
        budget.file_passes += 1
        record = dict(path=resolved, bytes=before.st_size, sha256=digest.hexdigest(), state=state(after))
        return record, bytes(parts) if keep else None
    finally:
        os.close(fd)


def search_paths(metadata, source_path, defaults):
    # These are conservative candidate paths, not glibc's inherited RPATH model.
    raw = metadata['runpath'] if metadata['runpath'] is not None else metadata['rpath']
    origin = str(Path(source_path).parent)
    paths = [] if raw is None else raw.split(':')
    need(len(paths) <= 16 and all(paths), 'empty or excessive dynamic search paths')
    expanded = []
    for path in paths:
        value = path.replace('${ORIGIN}', origin).replace('$ORIGIN', origin)
        need('$' not in value and Path(value).is_absolute(), 'unsupported relative/token loader path')
        need(len(value) <= 4096, 'expanded loader path cap')
        expanded.append(os.path.normpath(value))
    need(1 <= len(defaults) <= 8, 'fixed search-directory count')
    for path in defaults:
        need(type(path) is str and Path(path).is_absolute() and len(path) <= 4096, 'fixed search-directory path')
    return tuple(dict.fromkeys([*expanded, *defaults]))


def resolve_needed(name, directories):
    need(type(name) is str and name not in ('', '.', '..') and '/' not in name and '$' not in name,
         'plain dependency basename required')
    providers, aliases = {}, []
    for directory in directories:
        candidate = str(Path(directory)/name)
        try:
            resolved = canonical(candidate)
        except FileNotFoundError:
            continue
        st = os.stat(resolved, follow_symlinks=False)
        need(stat.S_ISREG(st.st_mode), 'dependency candidate not ordinary')
        providers.setdefault((st.st_dev, st.st_ino), resolved)
        aliases.append(dict(path=candidate, target=resolved))
    need(len(providers) == 1, 'missing or ambiguous dependency: '+name)
    return next(iter(providers.values())), aliases


def collect(python_path, decoder_path, *, defaults=DEFAULT_DIRS,
            cache_path='/etc/ld.so.cache', preload_path='/etc/ld.so.preload'):
    budget = Budget()
    queue = deque((('python', python_path), ('decoder', decoder_path)))
    records, roots, edges, aliases, candidates = {}, {}, [], {}, set(defaults)
    while queue:
        budget.checkpoint()
        role, request = queue.popleft()
        resolved = canonical(request)
        aliases[request] = resolved
        if role in ('python', 'decoder'): roots[role] = resolved
        if resolved in records: continue
        need(len(records) < NODE_CAP, 'ELF node count')
        record, data = snapshot(request, budget, keep=True)
        metadata = parse_elf(data)
        del data
        record['elf'] = metadata
        records[resolved] = record
        dirs = search_paths(metadata, resolved, defaults)
        candidates.update(dirs)
        if metadata['interpreter'] is not None:
            loader = metadata['interpreter']
            edges.append(dict(source=resolved, kind='PT_INTERP', name=loader, provider=canonical(loader)))
            queue.append(('loader', loader))
        for name in metadata['needed']:
            provider, found = resolve_needed(name, dirs)
            for item in found: aliases[item['path']] = item['target']
            edges.append(dict(source=resolved, kind='DT_NEEDED', name=name, provider=provider))
            queue.append(('dependency', provider))
        need(len(edges) <= EDGE_CAP, 'dependency edge count')
    need(all(edge['provider'] in records for edge in edges), 'unclosed declared dependency edge')
    cache, _ = snapshot(cache_path, budget)
    absence = []
    for path in sorted({preload_path, *(str(Path(p)/'glibc-hwcaps') for p in candidates)}):
        budget.checkpoint()
        absence.append(dict(path=path, absent=not os.path.lexists(path)))
    # Recheck all complete bytes. This detects observed drift, not an atomic host lock.
    for record in [*records.values(), cache]:
        again, _ = snapshot(record['path'], budget)
        for key in ('bytes', 'sha256', 'state'):
            need(again[key] == record[key], 'host drift during inventory: '+record['path'])
    for path, target in aliases.items():
        need(canonical(path) == target, 'alias drift during inventory')
    for item in absence:
        need(item['absent'] == (not os.path.lexists(item['path'])), 'absence state drift')
    budget.checkpoint()
    return dict(schema='bcrypt-static-toolchain-017-v1', status='STATIC_TOOLCHAIN_IDENTITY_RECORDED',
                roots=roots, nodes=list(records.values()), edges=edges,
                aliases=[dict(path=p, target=t) for p, t in sorted(aliases.items())],
                fixed_search_directories=list(defaults), cache=cache, absence_checks=absence,
                hooks_absent=all(item['absent'] for item in absence),
                node_count=len(records), edge_count=len(edges),
                reads=dict(complete_passes=budget.file_passes, declared_content_bytes=budget.content_bytes,
                           eof_requests=budget.eof_requests, cap_bytes=TOTAL_CAP),
                execution_admitted=False, source_independently_reviewed=False,
                runtime_loader_verified=False, python_stdlib_and_dynamic_imports_qualified=False,
                windows_image_content_reads=0, decoder_processes=0,
                limitations=['Static candidate graph is not a native loader trace.',
                             'Parent namespace and unchanged host during each operation are trust assumptions.',
                             'DT_NEEDED does not describe dlopen plugins or CPython late imports.',
                             'File hashes and build IDs do not assert signatures or software safety.',
                             'Time checks do not impose a hard kernel I/O deadline.'])
