"""Prepared 017 native/evidence bridge, not an admitted CLI or manifest owner.

Inject the separately preloaded, byte-qualified legacy OutputDirectory,
encode_json and parse_listing functions. This does not run the 014/016 owners or
reuse their manifests. No default identity verifier or fabricated admission is
provided. The isolated outer controller remains responsible for exact source,
loader/import closure, both preparation receipts and one registered invocation.
"""
from __future__ import annotations
import os
import posixpath
try:
    import resource
except ModuleNotFoundError:  # The bridge is Linux-only; portable import still works.
    resource = None
import signal
import subprocess

from bcrypt_guarded_owner_017 import (CAPTURE_CAP, IMAGE_BASE, RVA_START, RVA_END,
    TARGET_BYTES, Refused, need, prepared_target)

REPORT_CAP = 65536
ENV = {'LANG': 'C', 'LC_ALL': 'C', 'TZ': 'UTC', 'DEBUGINFOD_URLS': ''}


def absolute(value):
    need(type(value) is str and 1 < len(value) <= 256 and value.startswith('/')
         and not value.startswith('//') and posixpath.normpath(value) == value
         and not value.endswith('/') and all(32 <= ord(c) < 127 for c in value),
         'PREPARATION')
    return value


def sha256_text(value):
    need(type(value) is str and len(value) == 64 and
         all(c in '0123456789abcdef' for c in value), 'PREPARATION')
    return value


def _single_native_thread():
    """Bounded self-task check immediately before the single-threaded preexec.

    Python thread bookkeeping alone cannot exclude native-library threads.
    Missing procfs evidence fails closed. This snapshot does not prevent a
    trusted library from creating a thread later; frozen preload policy remains
    an external prerequisite, not a sandbox guarantee.
    """
    try:
        with os.scandir('/proc/self/task') as tasks:
            first = next(tasks, None)
            need(first is not None and first.name == str(os.getpid()) and
                 next(tasks, None) is None, 'PREPARATION')
    except OSError:
        raise Refused('PREPARATION') from None


def child_limits():
    """Candidate limits: single-threaded preexec only; no parent limit mutation."""
    need(resource is not None, 'CLEANUP')
    for kind, bound in ((resource.RLIMIT_FSIZE, CAPTURE_CAP),
                        (resource.RLIMIT_AS, 268435456), (resource.RLIMIT_CPU, 2),
                        (resource.RLIMIT_CORE, 0), (resource.RLIMIT_NOFILE, 64)):
        resource.setrlimit(kind, (bound, bound))
        need(resource.getrlimit(kind) == (bound, bound), 'CLEANUP')
    for name in ('SIGPIPE', 'SIGXFZ', 'SIGXFSZ'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), signal.SIG_DFL)


class RetainedChild:
    """Direct-child lifecycle. No promise of descendant census or OS hard return."""
    def __init__(self, process, directory):
        self.process, self.directory, self.reaped = process, directory, False
        self.closed, self.kill_requested = False, False

    def wait(self, *, timeout):
        need(not self.closed, 'CLEANUP')
        try:
            code = self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            raise Refused('DEADLINE') from None
        need(type(code) is int, 'CLEANUP')
        self.reaped = True
        return code

    def poll(self):
        need(not self.closed, 'CLEANUP')
        code = self.process.poll()
        if code is not None:
            need(type(code) is int, 'CLEANUP')
            self.reaped = True
        return code

    def kill(self):
        need(not self.kill_requested and not self.closed, 'CLEANUP')
        self.kill_requested = True
        # Do not signal a reusable process/group id after known reap. A newly
        # exited but not yet reaped root cannot have its PID reused by the OS.
        if self.poll() is None:
            os.killpg(self.process.pid, signal.SIGKILL)

    def capture(self, cap):
        need(self.reaped and not self.closed and type(cap) is int and cap == CAPTURE_CAP,
             'CAPTURE')
        return (self.directory.read('tool-stdout.bin', cap),
                self.directory.read('tool-stderr.bin', cap))

    def close(self):
        need(not self.closed, 'CLEANUP')
        # No pipes: the sink retains/closes redirected descriptors exactly once.
        self.closed = True


class PreparedNativeBackend:
    """Concrete bridge with trusted *explicit* legacy function dependencies.

    Tests replace Popen and legacy functions with finite in-process stand-ins.
    Instantiation alone reads no file and launches nothing. This class does not
    authenticate the provided functions/paths or confer real-execution authority.
    """
    def __init__(self, output_path, tool_path, target_sha256, request_sha256, *,
                 output_factory, encode_json, parse_listing):
        self.output_path, self.tool_path = absolute(output_path), absolute(tool_path)
        self.target_sha = sha256_text(target_sha256)
        self.request_sha = sha256_text(request_sha256)
        need(all(callable(f) for f in (output_factory, encode_json, parse_listing)), 'PREPARATION')
        self.factory, self.encoder, self.parser = output_factory, encode_json, parse_listing
        self.directory, self.expected, self.used, self.launch_used = None, {}, False, False
        self.captures_open, self.closed = False, False

    def reserve(self):
        need(not self.used and not self.closed, 'REUSED')
        self.used = True
        self.directory = self.factory(self.output_path)
        marker = dict(schema='bcrypt-owner-reservation-017-v1', request_sha256=self.request_sha,
                      target_sha256=self.target_sha, retry_allowed=False,
                      execution_admitted=False)
        raw = self.encoder(marker, 1024)
        self.directory.create('consumed.json', raw, 1024)
        self.expected['consumed.json'] = raw

    def write(self, name, data):
        need(self.directory is not None and not self.closed, 'OUTPUT')
        if name == 'target.bin':
            prepared_target(data, data, self.target_sha)
            self.directory.create(name, data, TARGET_BYTES, mode=0o400)
            self.expected[name] = data
            return
        mapped = {'stdout.bin': 'tool-stdout.bin', 'stderr.bin': 'tool-stderr.bin'}
        need(name in mapped and self.captures_open and type(data) is bytes and
             len(data) < CAPTURE_CAP, 'CAPTURE')
        actual = mapped[name]
        # These bytes already came from the retained descriptor. Never truncate
        # or replace a failed native capture just to fit the reported buffer.
        need(self.directory.read(actual, CAPTURE_CAP) == data, 'CAPTURE')
        self.expected[actual] = data

    def target_spec(self):
        return dict(runtime_function_index=302, section_index=0,
                    file_start=RVA_START, file_end=RVA_END,
                    rva_start=RVA_START, rva_end=RVA_END,
                    preferred_vma_start=IMAGE_BASE+RVA_START, sha256=self.target_sha)

    def launch(self, target):
        need(self.directory is not None and not self.closed and not self.launch_used, 'REUSED')
        self.launch_used = True
        need(os.name == 'posix' and resource is not None and hasattr(os, 'killpg'), 'CLEANUP')
        prepared_target(target, target, self.target_sha)
        need(self.directory.read('target.bin', TARGET_BYTES) == target, 'PREPARATION')
        need(signal.getsignal(signal.SIGCHLD) == signal.SIG_DFL, 'CLEANUP')
        stdout = self.directory.open_capture('tool-stdout.bin')
        self.expected['tool-stdout.bin'] = b''
        stderr = self.directory.open_capture('tool-stderr.bin')
        self.expected['tool-stderr.bin'] = b''
        self.captures_open = True
        self.directory.durable_readback(self.expected)
        start, end = IMAGE_BASE + RVA_START, IMAGE_BASE + RVA_END
        argv = [self.tool_path, '-D', '-z', '-b', 'binary', '-m', 'i386:x86-64', '-M',
                'intel', '--insn-width=16', '--wide', '--show-raw-insn',
                '--visualize-jumps=off', '--disassembler-color=off',
                f'--adjust-vma={start:#x}', f'--start-address={start:#x}',
                f'--stop-address={end:#x}', 'target.bin']
        _single_native_thread()
        process = subprocess.Popen(argv, cwd=self.output_path, env=dict(ENV),
            stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, shell=False,
            close_fds=True, start_new_session=True, restore_signals=True,
            preexec_fn=child_limits)
        return RetainedChild(process, self.directory)

    def parse(self, stdout, target):
        rows = self.parser(stdout, target, self.target_spec())
        need(type(rows) is list and 1 <= len(rows) <= 103, 'LISTING')
        result = []
        for row in rows:
            need(type(row) is dict and row.keys() == {
                'file_offset', 'rva', 'preferred_vma', 'length', 'raw_hex', 'assembly'}, 'LISTING')
            need(type(row['rva']) is int and type(row['file_offset']) is int and
                 row['file_offset'] == row['rva'] and type(row['preferred_vma']) is int and
                 row['preferred_vma'] == IMAGE_BASE + row['rva'], 'LISTING')
            result.append({key: row[key] for key in ('rva', 'length', 'raw_hex', 'assembly')})
        return result  # The core then verifies exact 103-byte coverage and bounds.

    def candidate(self, report):
        need(self.directory is not None and not self.closed, 'OUTPUT')
        raw = self.encoder(report, REPORT_CAP)
        self.directory.create('report.json', raw, REPORT_CAP)
        self.expected['report.json'] = raw
        # Unknown/unreaped capture may still change. Do not manufacture stability.
        if report['capture_stable'] or not self.captures_open:
            self.directory.durable_readback(self.expected)

    def close(self):
        need(not self.closed, 'OUTPUT')
        self.closed = True
        if self.directory is not None:
            self.directory.close()
