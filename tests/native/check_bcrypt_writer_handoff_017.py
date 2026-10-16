"""Finite writer-boundary development evidence, NOT writer-domain authority.

Four real toy Python children at most: inheritable-FD counterexample, the existing
OwnerTransport, the existing PreparedNativeBackend, and a same-UID path writer.
No actual bcrypt image, decoder, preparation, host process/fd scan or cleanup of
user files. Normal unit discovery never calls run(). Keep every failure prefix.
"""
from pathlib import Path
import hashlib
import os
import stat
import subprocess
import sys

if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from bcrypt_admission_contract_017 import encode, decode, equal
from bcrypt_disassembly_io_014 import OutputDirectory
from bcrypt_disassembly_model_014 import encode_json
from bcrypt_disassembly_listing_014 import parse_listing
from bcrypt_guarded_owner_017 import need
from bcrypt_outer_transport_017 import OwnerTransport
from bcrypt_owner_backend_017 import PreparedNativeBackend, ENV, CAPTURE_CAP, _single_native_thread

REQUEST = hashlib.sha256(b'017 synthetic writer handoff, not an observation').hexdigest()
TARGET = b'\x90' * 102 + b'\xc3'
CAP = 4096
CHILD_BODY = '''import json, os
probes = PROBES
matches = []
for fd, dev, ino, kind in probes:
    try:
        s = os.fstat(fd)
        same = [s.st_dev, s.st_ino, s.st_mode & 0o170000] == [dev, ino, kind]
    except OSError:
        same = False
    matches.append(same)
report = dict(status='owner_complete_unreviewed', binding=dict(request_sha256=REQUEST),
              synthetic=True, inherited_matches=matches, observation_accepted=False)
raw = (json.dumps(report, sort_keys=True, separators=(',', ':'))+'\\n').encode('ascii')
assert os.write(1, raw) == len(raw)
'''


def probe_source(python, probes):
    need(type(python) is str and python.startswith('/') and len(python) < 128 and
         all(c.isascii() and not c.isspace() for c in python), 'HANDOFF_PYTHON_PATH')
    need(type(probes) is list and len(probes) == 2 and
         all(type(p) is list and len(p) == 4 and all(type(n) is int and 0 <= n < 2**64 for n in p)
             and p[0] > 2 and p[3] in (stat.S_IFREG, stat.S_IFDIR) for p in probes) and
         probes[0][0] != probes[1][0], 'HANDOFF_PROBES')
    # One optional shebang argument: no /usr/bin/env search, no site initialization.
    return ('#!'+python+' -S\n'+CHILD_BODY.replace('PROBES', repr(probes)).replace('REQUEST', repr(REQUEST))).encode()


def check_probe(raw, expected):
    need(type(expected) is bool, 'HANDOFF_EXPECTATION')
    value = decode(raw, CAP)
    equal(value, dict(status='owner_complete_unreviewed', binding=dict(request_sha256=REQUEST),
                     synthetic=True, inherited_matches=[expected, expected], observation_accepted=False))
    return value


def save(root, name, value):
    raw = encode(value, 32768)
    with (root/name).open('xb') as stream:
        stream.write(raw); stream.flush(); os.fsync(stream.fileno())


def read_small(path):
    with path.open('rb') as stream:
        raw = stream.read(CAP+1)
    need(len(raw) <= CAP, 'HANDOFF_CAPTURE_CAP')
    return raw


def direct(root, name, argv, *, close_fds):
    """Exactly one known non-forking toy child; failures do not authorize retry."""
    out, err = root/(name+'.stdout'), root/(name+'.stderr')
    save(root, name+'-launch.json', dict(argv=argv, environment=ENV,
         close_fds=close_fds, timeout_seconds=3, toy=True))
    process = None
    try:
        with out.open('xb') as stdout, err.open('xb') as stderr:
            process = subprocess.Popen(argv, cwd=root, env=dict(ENV), stdin=subprocess.DEVNULL,
                stdout=stdout, stderr=stderr, close_fds=close_fds, shell=False)
            code = process.wait(timeout=3)
        save(root, name+'-exit.json', dict(exit_code=code, reaped=True, retry=False))
        need(type(code) is int and code == 0 and read_small(err) == b'', 'HANDOFF_CHILD')
        return read_small(out)
    finally:
        if process is not None and process.poll() is None:
            try: process.kill()
            finally: process.wait(timeout=1)


def native(root, program):
    backend = PreparedNativeBackend(str(root/'backend'), str(program),
        hashlib.sha256(TARGET).hexdigest(), REQUEST, output_factory=OutputDirectory,
        encode_json=encode_json, parse_listing=parse_listing)
    child = None
    try:
        backend.reserve(); backend.write('target.bin', TARGET)
        child = backend.launch(TARGET)  # Unmodified GNU argv; toy program ignores it.
        code = child.wait(timeout=3)
        stdout, stderr = child.capture(CAPTURE_CAP)
        need(type(code) is int and code == 0 and not stderr and len(stdout) <= CAP, 'HANDOFF_NATIVE')
        backend.write('stdout.bin', stdout); backend.write('stderr.bin', stderr)
        value = check_probe(stdout, False)
        save(root, 'backend-exit.json', dict(exit_code=code, reaped=child.reaped,
             parse_called=False, toy=True, request_sha256=REQUEST))
        return value
    finally:
        try:
            if child is not None:
                try:
                    if child.poll() is None:
                        try: child.kill()
                        finally: child.wait(timeout=1)
                finally: child.close()
        finally: backend.close()


def run(output):
    need(sys.platform == 'linux' and sys.flags.isolated and sys.flags.no_site and
         sys.dont_write_bytecode and not sys.flags.optimize, 'HANDOFF_ISOLATION')
    _single_native_thread()
    root = Path(output).absolute()
    need(str(root.parent.resolve(strict=True)) == str(root.parent), 'HANDOFF_PATH')
    root.mkdir(mode=0o700)  # A second call cannot reuse this evidence directory.
    python = os.path.realpath(sys.executable)
    save(root, 'started.json', dict(schema='bcrypt-writer-handoff-dev-v1', python=python,
         python_sha256=hashlib.sha256(Path(python).read_bytes()).hexdigest(),
         max_toy_children=4, decoder_processes=0, formal_observations=0))
    descriptors = []
    try:
        sentinel = root/'sentinel.bin'
        fd = os.open(sentinel, os.O_RDWR|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW, 0o600)
        descriptors.append(fd); os.write(fd, b'before\n'); os.fsync(fd)
        directory = os.open(root, os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
        descriptors.append(directory)
        probes = []
        for descriptor in descriptors:
            os.set_inheritable(descriptor, True)
            need(os.get_inheritable(descriptor), 'HANDOFF_INHERITABLE')
            s = os.fstat(descriptor)
            probes.append([descriptor, s.st_dev, s.st_ino, stat.S_IFMT(s.st_mode)])
        save(root, 'probes.json', dict(probes=probes, synthetic_owned_descriptors=True))
        program = root/'probe.py'
        with program.open('xb') as stream: stream.write(probe_source(python, probes))
        program.chmod(0o500)
        # Sensitivity control: prove these exact open-file identities CAN cross exec.
        check_probe(direct(root, 'inheritable-control', [python, '-I', '-S', '-B', str(program)],
                           close_fds=False), True)
        spec = dict(schema='bcrypt-owner-launch-017-v1', request_sha256=REQUEST,
            argv=[python, '-I', '-S', '-B', str(program)], cwd=str(root),
            output_dir=str(root/'transport'), environment=ENV, wall_ms=3000)
        raw = encode(spec)
        result = OwnerTransport(raw, hashlib.sha256(raw).hexdigest()).run()
        save(root, 'transport-result.json', result)
        need(result['status'] == 'owner_transport_complete_unreviewed', 'HANDOFF_TRANSPORT')
        check_probe(read_small(root/'transport'/'owner-final.json'), False)
        native(root, program)
        # Passing the descriptor tests does NOT stop a same-UID unrelated path writer.
        peer = root/'peer.py'
        peer_code = ('import os\nfd=os.open('+repr(str(sentinel))+',os.O_WRONLY|os.O_TRUNC|os.O_NOFOLLOW)\n'
                     'try:\n assert os.write(fd,b"peer-write\\n")==11\n os.fsync(fd)\n'
                     'finally: os.close(fd)\nos.write(1,b"same-uid-path-write\\n")\n')
        with peer.open('x') as stream: stream.write(peer_code)
        need(stat.S_IMODE(root.stat().st_mode) == 0o700 and
             stat.S_IMODE(os.fstat(fd).st_mode) == 0o600, 'HANDOFF_MODE')
        peer_raw = direct(root, 'same-uid-control', [python, '-I', '-S', '-B', str(peer)], close_fds=True)
        need(peer_raw == b'same-uid-path-write\n' and os.pread(fd, CAP, 0) == b'peer-write\n',
             'HANDOFF_PEER_CONTROL')
        result = dict(schema='bcrypt-writer-handoff-dev-result-v1',
            status='HANDOFF_CONTROLS_MATCHED_NOT_WRITER_QUALIFIED', toy_children=4,
            leaking_control_detected=True, owner_inheritance_blocked=True,
            backend_inheritance_blocked=True, same_uid_path_writer_succeeded=True,
            public_modes_changed=False, target_source='synthetic_102_NOP_plus_RET_not_executed',
            no_descriptor_export_proved=False, writer_domain_qualified=False,
            decoder_processes=0, formal_observations=0, observation_accepted=False, clears_hold=False)
        save(root, 'completed.json', result)
        return result
    except BaseException:
        save(root, 'refused.json', dict(status='refused', retry=False, writer_domain_qualified=False))
        raise
    finally:
        for descriptor in reversed(descriptors): os.close(descriptor)


if __name__ == '__main__':
    need(len(sys.argv) == 3 and sys.argv[1] == '--execute-registered-toy-check', 'HANDOFF_ARGV')
    run(sys.argv[2])
