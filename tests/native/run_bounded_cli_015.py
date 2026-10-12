"""One phase-bound 015 owner, bounded streams and deadline after final close."""
import hashlib
import json
import os
import selectors
import signal
import stat
import subprocess
import sys
import time
import types

PYTHON = '/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3.12'
BOOTSTRAP = '/workspace/scratch/3d56575ba9f2/mqb-work/producer-clean-handoff/tests/native/run_bcrypt_disassembly_015.py'
EVIDENCE = '/workspace/scratch/a8b3faf101c5/evidence-015'
ENV = {'LANG': 'C', 'LC_ALL': 'C', 'TZ': 'UTC', 'DEBUGINFOD_URLS': ''}
CAPS = {'consumed.json': 1024, 'tool-stdout.bin': 1, 'tool-stderr.bin': 66, 'report.json': 8192}


def bounded_read(path, cap, digest):
    if not (type(path) is str and 1 < len(path) <= 256 and
            os.path.isabs(path) and os.path.realpath(path) == path):
        raise ValueError('canonical input')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= cap <= 32768:
            raise ValueError('input cap')
        raw = os.read(fd, cap + 1)
        after, current = os.fstat(fd), os.stat(path, follow_symlinks=False)
        keys = ('st_dev', 'st_ino', 'st_mode', 'st_nlink', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
        if (len(raw) != before.st_size or len(raw) > cap or os.read(fd, 1) or
                any(getattr(before, k) != getattr(after, k) or
                    getattr(before, k) != getattr(current, k) for k in keys) or
                hashlib.sha256(raw).hexdigest() != digest or os.path.realpath(path) != path):
            raise ValueError('input identity')
        return raw
    finally:
        os.close(fd)


def load(manifest_path, expected_sha):
    raw = bounded_read(manifest_path, 32768, expected_sha)
    # This bounded initial parse only locates the bootstrap's externally pinned
    # identity. No phase core executes before the bootstrap's strict decoder.
    rec = json.loads(raw)['sources']['run_bcrypt_disassembly_015.py']
    if rec['path'] != BOOTSTRAP:
        raise ValueError('bootstrap path')
    source = bounded_read(BOOTSTRAP, 32768, rec['sha256'])
    bootstrap = types.ModuleType('bcrypt015_cli_bootstrap')
    bootstrap.__file__ = BOOTSTRAP
    exec(compile(source, BOOTSTRAP, 'exec'), bootstrap.__dict__)
    manifest = bootstrap._decode(raw)
    canonical = (json.dumps(manifest, ensure_ascii=True, allow_nan=False,
                            sort_keys=True, separators=(',', ':')) + '\n').encode('ascii')
    if canonical != raw:
        raise ValueError('canonical manifest')
    model, _ = bootstrap._load_sources(manifest)
    model.validate_manifest(manifest)
    if model.encode_json(manifest, 32768) != raw:
        raise ValueError('canonical manifest')
    return manifest, model, sys.modules['bcrypt_disassembly_io_014']


def output_directory(io, path):
    class BoundedOutput(io.OutputDirectory):
        @staticmethod
        def _caps():
            return dict(CAPS)
    return BoundedOutput(path)


def run_once(manifest_path, expected_sha, destination):
    started = time.monotonic_ns()
    manifest, model, io = load(manifest_path, expected_sha)
    phase = manifest['phase']
    seconds = model.limits_for(phase)['outer_seconds']
    if phase not in ('selection', 'actual') or seconds != (12 if phase == 'selection' else 20):
        raise ValueError('phase deadline')
    fixed = EVIDENCE + ('/cli-selection-015' if phase == 'selection' else '/cli-actual-015')
    if destination == manifest['output_dir'] or manifest['mode'] == 'actual' and destination != fixed:
        raise ValueError('outer output scope')
    deadline = started + seconds * 1_000_000_000
    argv = [PYTHON, '-I', '-B', BOOTSTRAP, manifest_path, expected_sha]
    out, selector, process = None, None, None
    buffers, expected = {'stdout': bytearray(), 'stderr': bytearray()}, {}
    fault, reaped, rc = None, False, None
    try:
        out = output_directory(io, destination)
        marker = model.encode_json({
            'schema': 'bcrypt015-cli-submission-v1', 'call_id': manifest['call_id'], 'phase': phase,
            'manifest_sha256': expected_sha,
            'argv_sha256': hashlib.sha256(model.encode_json(argv, 8192)).hexdigest(),
            'attempt_consumed': True, 'retry_allowed': False,
            'stdout_cap': 0, 'stderr_cap': 65, 'detection_bytes_per_stream': 1,
            'controller_seconds': seconds,
        }, CAPS['consumed.json'])
        out.create('consumed.json', marker, CAPS['consumed.json'])
        expected['consumed.json'] = marker
        out.durable_readback(expected)
        if time.monotonic_ns() > deadline or signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL:
            raise ValueError('controller prelaunch')
        selector = selectors.DefaultSelector()
        try:
            process = subprocess.Popen(argv, stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       cwd=destination, env=ENV, close_fds=True,
                                       start_new_session=True)
            for key, pipe, cap in (('stdout', process.stdout, 0), ('stderr', process.stderr, 65)):
                os.set_blocking(pipe.fileno(), False)
                selector.register(pipe, selectors.EVENT_READ, (key, cap))
            while selector.get_map():
                remaining = (deadline - time.monotonic_ns()) / 1_000_000_000
                if remaining <= 0:
                    raise TimeoutError()
                for selected, _ in selector.select(min(remaining, 0.1)):
                    key, cap = selected.data
                    chunk = os.read(selected.fileobj.fileno(), cap + 1 - len(buffers[key]))
                    if not chunk:
                        selector.unregister(selected.fileobj)
                        continue
                    buffers[key].extend(chunk)
                    if len(buffers[key]) > cap:
                        raise OverflowError()
            rc = process.wait(timeout=max(0.001, (deadline - time.monotonic_ns()) / 1e9))
            reaped = True
            if time.monotonic_ns() > deadline:
                fault = 'CONTROLLER_DEADLINE'
        except BaseException as error:
            fault = ('CONTROLLER_DEADLINE' if isinstance(error, (TimeoutError, subprocess.TimeoutExpired))
                     else 'CLI_OUTPUT_CAP' if isinstance(error, OverflowError) else 'CONTROLLER_FAILURE')
            if process is not None:
                rc = process.poll()
                if rc is None:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except OSError:
                        pass
                    try:
                        rc = process.wait(timeout=1)
                    except (OSError, subprocess.TimeoutExpired):
                        pass
                reaped = rc is not None
        result = {
            'schema': 'bcrypt015-cli-result-v1', 'call_id': manifest['call_id'], 'phase': phase,
            'manifest_sha256': expected_sha, 'argv': argv, 'environment': ENV,
            'owner_returncode': rc, 'owner_reaped': reaped, 'controller_fault': fault,
            'owner_tool_cleanup_not_assumed_if_controller_fault': True,
            'elapsed_ns_before_persistence': time.monotonic_ns() - started,
            'completion_candidate': rc == 0 and reaped and fault is None and not any(buffers.values()),
            'completion_requires_clean_controller_exit': True,
        }
        for key, buffer in buffers.items():
            raw, name = bytes(buffer), 'tool-' + key + '.bin'
            out.create(name, raw, CAPS[name])
            expected[name] = raw
            result[key] = {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}
        raw = model.encode_json(result, CAPS['report.json'])
        out.create('report.json', raw, CAPS['report.json'])
        expected['report.json'] = raw
        out.durable_readback(expected)
        out.close()
        out = None
    finally:
        closing = [selector] if selector is not None else []
        if process is not None:
            closing += [pipe for pipe in (process.stdout, process.stderr) if pipe is not None]
        if out is not None:
            closing.append(out)
        failed = False
        for handle in closing:
            try:
                handle.close()
            except BaseException:
                failed = True
        if failed:
            raise OSError('controller close failure')
    if time.monotonic_ns() > deadline:
        raise TimeoutError('late persistence or cleanup')
    return result


def main(argv=None):
    try:
        args = sys.argv[1:] if argv is None else argv
        if type(args) is not list or len(args) != 3:
            raise ValueError('arguments')
        result = run_once(*args)
        raw = (json.dumps({key: result[key] for key in
                           ('call_id', 'completion_candidate', 'owner_returncode', 'controller_fault')},
                          ensure_ascii=True, allow_nan=False, sort_keys=True,
                          separators=(',', ':')) + '\n').encode('ascii')
        if len(raw) > 256:
            raise ValueError('controller banner cap')
        code = 0 if result['completion_candidate'] else 1
    except BaseException:
        raw, code = b'{"controller_setup_or_persistence_failure":true}\n', 2
    try:
        if os.write(1, raw) != len(raw):
            return 2
    except BaseException:
        return 2
    return code


if __name__ == '__main__':
    sys.exit(main())
