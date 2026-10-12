"""Bounded 015 bootstrap: verify the entire phase set before core compilation.

The external admission verifies this executing entrypoint before launch.
This module verifies every phase source again and compiles retained bytes only.
"""
import hashlib
import json
import os
import stat
import sys
import time
import types


def _check(condition):
    if not condition:
        raise ValueError('BOOTSTRAP_REFUSED')


def _source_names(phase):
    _check(type(phase) is str and phase in ('selection', 'actual'))
    names = ('bcrypt_disassembly_model_014.py',)
    if phase == 'actual':
        names += ('bcrypt_disassembly_listing_014.py',)
    return names + ('bcrypt_disassembly_io_014.py', 'bcrypt_disassembly_model_015.py',
                    'bcrypt_selection_015.py', 'prepare_bcrypt_selection_015.py'
                    if phase == 'selection' else 'bcrypt_disassembly_owner_015.py',
                    'run_bcrypt_disassembly_015.py')


def _path(path):
    _check(type(path) is str and 1 < len(path) <= 256 and path.startswith('/')
           and all(32 <= ord(c) < 127 for c in path)
           and os.path.normpath(path) == path and os.path.realpath(path) == path)


def _snapshot(st):
    return (st.st_dev, st.st_ino, st.st_mode, st.st_nlink, st.st_size,
            st.st_mtime_ns, st.st_ctime_ns)


def _read(path, cap, expected=None):
    _path(path)
    _check(type(cap) is int and 1 <= cap <= 32768)
    fd = None
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        before = os.fstat(fd)
        _check(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= cap)
        _check(_snapshot(os.stat(path, follow_symlinks=False)) == _snapshot(before))
        chunks, remaining = [], before.st_size
        while remaining:
            raw = os.read(fd, min(65536, remaining))
            _check(bool(raw))
            chunks.append(raw)
            remaining -= len(raw)
        _check(not os.read(fd, 1))
        after = os.fstat(fd)
        _path(path)
        _check(_snapshot(before) == _snapshot(after)
               == _snapshot(os.stat(path, follow_symlinks=False)))
        raw = b''.join(chunks)
        _check(expected is None or hashlib.sha256(raw).hexdigest() == expected)
        return raw
    except OSError as error:
        raise ValueError('BOOTSTRAP_REFUSED') from error
    finally:
        if fd is not None:
            os.close(fd)


def _decode(raw):
    _check(type(raw) is bytes and 0 < len(raw) <= 32768)
    text = raw.decode('ascii')
    depth, quoted, escaped, nodes = 0, False, False, 0
    for c in text:
        if quoted:
            if escaped:
                escaped = False
            elif c == '\\':
                escaped = True
            elif c == '"':
                quoted = False
            continue
        if c == '"':
            quoted = True
        elif c in '[{':
            depth += 1
            _check(depth <= 16)
        elif c in ']}':
            depth -= 1
            _check(depth >= 0)
        if c in '[{,:"':
            nodes += 1
            _check(nodes <= 32768)
    _check(not quoted and depth == 0)

    def pairs(items):
        result = {}
        for key, value in items:
            _check(key not in result)
            result[key] = value
        return result

    def integer(value):
        _check(len(value) <= 21)
        parsed = int(value)
        _check(-(1 << 63) <= parsed < (1 << 64))
        return parsed

    def reject(_):
        raise ValueError('BOOTSTRAP_REFUSED')

    return json.loads(text, object_pairs_hook=pairs, parse_int=integer,
                      parse_float=reject, parse_constant=reject)


def _load_sources(manifest):
    _check(type(manifest) is dict)
    names = _source_names(manifest.get('phase'))
    sources = manifest.get('sources')
    _check(type(sources) is dict and set(sources) == set(names))
    directory = os.path.dirname(os.path.realpath(__file__))
    verified = {}
    for name in names:
        record = sources[name]
        _check(type(record) is dict and set(record) == {'path', 'bytes', 'sha256'})
        _check(record['path'] == directory + '/' + name
               and type(record['bytes']) is int and 0 < record['bytes'] <= 32768
               and type(record['sha256']) is str and len(record['sha256']) == 64
               and all(c in '0123456789abcdef' for c in record['sha256']))
        raw = _read(record['path'], 32768, record['sha256'])
        _check(len(raw) == record['bytes'])
        verified[name] = raw
    loaded = {}
    for name in names[:-1]:
        module_name = name[:-3]
        module = types.ModuleType(module_name)
        module.__file__ = sources[name]['path']
        sys.modules[module_name] = module
        exec(compile(verified[name], module.__file__, 'exec'), module.__dict__)
        loaded[module_name] = module
    return loaded['bcrypt_disassembly_model_015'], loaded[names[-2][:-3]]


def main(argv=None):
    started = time.monotonic_ns()
    try:
        args = sys.argv[1:] if argv is None else argv
        _check(type(args) is list and len(args) == 2)
        path, digest = args
        _check(type(digest) is str and len(digest) == 64
               and all(c in '0123456789abcdef' for c in digest))
        raw = _read(path, 32768, digest)
        manifest = _decode(raw)
        model, phase = _load_sources(manifest)
        model.validate_manifest(manifest)
        _check(model.encode_json(manifest, 32768) == raw
               and os.path.realpath(sys.executable) == manifest['identities']['python']['path'])
        return phase.observe(manifest, raw, path, digest, started)
    except BaseException:
        return 2, b'BOOTSTRAP_REFUSED\n'


def _finish(code, diagnostic):
    try:
        if diagnostic:
            os.write(2, diagnostic)
    except BaseException:
        code = 2
    os._exit(code)


if __name__ == '__main__':
    _finish(*main())
