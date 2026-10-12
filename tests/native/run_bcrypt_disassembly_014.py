"""Verify all five bounded source blobs before loading any 014 module bytes."""
import hashlib
import json
import os
import re
import stat
import sys
import time
import types

SOURCE_NAMES = (
    'bcrypt_disassembly_model_014.py', 'bcrypt_disassembly_listing_014.py',
    'bcrypt_disassembly_io_014.py', 'bcrypt_disassembly_owner_014.py',
    'run_bcrypt_disassembly_014.py',
)
DIAGNOSTIC = b'BOOTSTRAP_REFUSED\n'


def _check(condition):
    if not condition:
        raise ValueError('BOOTSTRAP_REFUSED')


def _path(path):
    _check(type(path) is str and 1 < len(path) <= 256 and
           all(32 <= ord(c) < 127 for c in path) and
           os.path.isabs(path) and os.path.realpath(path) == path)


def _read(path, cap, expected=None):
    _path(path)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        before = os.fstat(fd)
        _check(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= cap)
        chunks, count = [], 0
        while count <= cap:
            raw = os.read(fd, min(65536, cap + 1 - count))
            if not raw:
                break
            chunks.append(raw)
            count += len(raw)
        after = os.fstat(fd)
        fields = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns', 'st_mode')
        _check(count == before.st_size and count <= cap and
               all(getattr(before, k) == getattr(after, k) for k in fields))
        current = os.stat(path, follow_symlinks=False)
        _check(current.st_dev == before.st_dev and current.st_ino == before.st_ino)
        result = b''.join(chunks)
        if expected is not None:
            _check(hashlib.sha256(result).hexdigest() == expected)
        return result
    finally:
        os.close(fd)


def _decode(raw):
    # Manifest-only preflight; the verified model performs the full strict decode.
    depth = 0
    quoted = escaped = False
    for byte in raw:
        _check(byte < 128)
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            depth += 1
        elif byte in (93, 125):
            depth -= 1
        _check(0 <= depth <= 16)
    _check(not quoted and depth == 0)

    def pairs(items):
        result = {}
        for key, value in items:
            _check(key not in result)
            result[key] = value
        return result

    def integer(value):
        _check(len(value) <= 20)
        result = int(value)
        _check(-(1 << 63) <= result < (1 << 64))
        return result

    def refuse(_):
        raise ValueError('BOOTSTRAP_REFUSED')

    return json.loads(raw.decode('ascii'), object_pairs_hook=pairs, parse_int=integer,
                      parse_float=refuse, parse_constant=refuse)


def _load_sources(manifest):
    source_dir = os.path.dirname(os.path.realpath(__file__))
    records = manifest['sources']
    _check(type(records) is dict and set(records) == set(SOURCE_NAMES))
    blobs = {}
    for name in SOURCE_NAMES:
        rec = records[name]
        _check(type(rec) is dict and set(rec) == {'path', 'bytes', 'sha256'} and
               rec['path'] == os.path.join(source_dir, name) and
               type(rec['bytes']) is int and 0 < rec['bytes'] <= 32768 and
               type(rec['sha256']) is str and re.fullmatch('[0-9a-f]{64}', rec['sha256']))
        raw = _read(rec['path'], 32768, rec['sha256'])
        _check(len(raw) == rec['bytes'])
        blobs[name] = raw
    # No source re-open between verification and compile/exec; imports hit these modules.
    for name in SOURCE_NAMES[:-1]:
        module_name = name[:-3]
        module = types.ModuleType(module_name)
        module.__file__ = records[name]['path']
        sys.modules[module_name] = module
        exec(compile(blobs[name], module.__file__, 'exec'), module.__dict__)
    return sys.modules[SOURCE_NAMES[0][:-3]], sys.modules[SOURCE_NAMES[3][:-3]]


def main(argv=None):
    start_ns = time.monotonic_ns()
    try:
        args = sys.argv[1:] if argv is None else argv
        _check(type(args) is list and len(args) == 2 and
               type(args[1]) is str and re.fullmatch('[0-9a-f]{64}', args[1]))
        path, digest = args
        raw = _read(path, 32768, digest)
        manifest = _decode(raw)
        model, owner = _load_sources(manifest)
        model.validate_manifest(manifest)
        _check(model.decode_json(raw, 32768) == manifest)
        _check(os.path.realpath(sys.executable) == manifest['identities']['python']['path'])
        return owner.observe(manifest, raw, path, digest, start_ns)
    except BaseException:
        return 2, DIAGNOSTIC


if __name__ == '__main__':
    code, diagnostic = main()
    try:
        if diagnostic:
            os.write(2, diagnostic)
    except BaseException:
        code = 2
    os._exit(code)
