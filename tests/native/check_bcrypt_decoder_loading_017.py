"""Explicit dev008: two fixed GNU processes on self-created NOP/RET bytes.

Baseline uses the unchanged production backend. Diagnostic copies the observed
argv and adds only LD_DEBUG=libs; it is NOT the production environment. Neither
run is a formal target observation or a transferable loader/writer approval.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

if __name__ == '__main__':
    if not (sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
            and not sys.flags.optimize):
        raise SystemExit('ISOLATED_DEVELOPMENT_REQUIRED')
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from bcrypt_owner_backend_017 import (PreparedNativeBackend, RetainedChild, ENV,
    CAPTURE_CAP, child_limits, _single_native_thread)
from bcrypt_disassembly_io_014 import OutputDirectory
from bcrypt_disassembly_model_014 import encode_json
from bcrypt_disassembly_listing_014 import parse_listing
from bcrypt_guarded_owner_017 import checked_listing, need, Refused, RVA_START
from bcrypt_writer_scope_017 import DescendantScope
from bcrypt_toolchain_017 import collect
from check_bcrypt_runtime_evidence_017 import save_new

DECODER = '/usr/bin/x86_64-linux-gnu-objdump'
FIXTURE = b'\x90' * 102 + b'\xc3'
FIXTURE_SHA = hashlib.sha256(FIXTURE).hexdigest()
PLAN_NAME = 'decoder-dev008-source.json'


def fixture_listing(rows):
    rows = checked_listing(rows, FIXTURE)
    need(len(rows) == 103, 'DEV_INSTRUCTION_COUNT')
    for i, row in enumerate(rows):
        need(row == dict(rva=RVA_START+i, length=1,
             raw_hex='90' if i < 102 else 'c3', assembly='nop' if i < 102 else 'ret'),
             'DEV_INSTRUCTION')
    return rows


def loader_events(raw, pid):
    """Strict glibc LD_DEBUG=libs projection. Search attempts are NOT loaded files.

    Raw stderr remains the primary evidence. Text is not a public stable ABI;
    an unrecognized record is a refusal, not silently dropped evidence.
    """
    need(type(raw) is bytes and 0 < len(raw) < CAPTURE_CAP and raw.endswith(b'\n'), 'DEV_DEBUG_BYTES')
    need(type(pid) is int and 1 < pid < 2**31, 'DEV_DEBUG_PID')
    try: lines = raw.decode('ascii').splitlines()
    except UnicodeError: raise Refused('DEV_DEBUG_ASCII') from None
    need(len(lines) <= 1024, 'DEV_DEBUG_ROWS')
    events, searches = [], 0
    for line in lines:
        need(len(line) <= 8192 and all(c == '\t' or 32 <= ord(c) < 127 for c in line), 'DEV_DEBUG_ROW')
        if not line.strip(): continue
        match = re.fullmatch(r'\s*([0-9]{1,10}):\s*(.*)', line)
        need(match is not None and int(match[1]) == pid, 'DEV_DEBUG_PID')
        text = match[2].strip()
        if not text: continue
        match = re.fullmatch(r'(calling init|calling fini|initialize program|transferring control):\s*(.*)', text)
        if match:
            kind, path = match.groups()
            if kind == 'calling fini':
                need(path.endswith('[0]'), 'DEV_DEBUG_NAMESPACE')
                path = path[:-3].strip()
            need(not path or (path.startswith(('/usr/lib/', '/lib/', '/lib64/', '/usr/bin/'))
                 and os.path.normpath(path) == path and len(path) <= 4096
                 and re.fullmatch(r'[/A-Za-z0-9_.+\-]+', path)), 'DEV_DEBUG_PATH')
            need(path or kind == 'calling fini', 'DEV_DEBUG_PATH')
            events.append(dict(kind=kind, path=path))
            need(len(events) <= 68, 'DEV_DEBUG_EVENTS')
        elif (re.fullmatch(r'find library=[A-Za-z0-9_.+\-]+ \[0\]; searching', text) or
              re.fullmatch(r'search (?:path|cache)=.+', text) or
              re.fullmatch(r'trying file=/[A-Za-z0-9_./+\-]+', text)):
            searches += 1
        else:
            raise Refused('DEV_DEBUG_UNKNOWN')
    init = [e['path'] for e in events if e['kind'] == 'calling init']
    fini = [e['path'] for e in events if e['kind'] == 'calling fini']
    start = [e['path'] for e in events if e['kind'] == 'initialize program']
    transfer = [e['path'] for e in events if e['kind'] == 'transferring control']
    need(start == transfer == [DECODER], 'DEV_DEBUG_ENTRY')
    need(1 <= len(init) <= 32 and len(init) == len(set(init)), 'DEV_DEBUG_INIT')
    need(len(fini) == len(set(fini)) and set(fini) == set(init) | {''}, 'DEV_DEBUG_FINI')
    kinds = [e['kind'] for e in events]
    need(kinds == ['calling init']*len(init) + ['initialize program','transferring control']
         + ['calling fini']*len(fini), 'DEV_DEBUG_ORDER')
    return dict(events=events, initialized=init, finalized=fini, search_records=searches,
                proves_all_mappings=False, production_environment=False)


def join_libraries(projected, graph):
    # Follow only the decoder's declared closure; the graph also contains Python.
    reachable = {graph['roots']['decoder']}
    for _ in range(32):
        more = {e['provider'] for e in graph['edges'] if e['source'] in reachable}
        if more <= reachable: break
        reachable |= more
    else: raise Refused('DEV_GRAPH_DEPTH')
    aliases = {row['path']: row['target'] for row in graph['aliases']}
    nodes = {row['path']: row for row in graph['nodes']}
    for path in nodes: aliases[path] = path
    selected = []
    for path in projected['initialized']:
        # No filesystem lookup using diagnostic text; only pre-read graph aliases.
        need(path in aliases and aliases[path] in nodes, 'DEV_LOADED_UNKNOWN')
        selected.append(aliases[path])
    need(len(selected) == len(set(selected)) and
         set(selected) == reachable - {graph['roots']['decoder']}, 'DEV_LOADED_SET')
    return [dict(path=p, bytes=nodes[p]['bytes'], sha256=nodes[p]['sha256']) for p in sorted(selected)]


def diagnostic_launch(backend, argv):
    """Development-only environment delta; never patch the production module."""
    need(not backend.launch_used and not backend.closed, 'DEV_REUSED')
    backend.launch_used = True
    need(backend.directory.read('target.bin',103) == FIXTURE, 'DEV_FIXTURE_CHANGED')
    stdout = backend.directory.open_capture('tool-stdout.bin')
    backend.expected['tool-stdout.bin'] = b''
    stderr = backend.directory.open_capture('tool-stderr.bin')
    backend.expected['tool-stderr.bin'] = b''
    backend.captures_open = True
    backend.directory.durable_readback(backend.expected)
    _single_native_thread()
    process = subprocess.Popen(list(argv), cwd=backend.output_path,
        env={**ENV, 'LD_DEBUG':'libs'}, stdin=subprocess.DEVNULL, stdout=stdout,
        stderr=stderr, shell=False, close_fds=True, start_new_session=True,
        restore_signals=True, preexec_fn=child_limits)
    return RetainedChild(process,backend.directory)


def run_case(root, case, prior_argv=None):
    need(case in ('baseline','diagnostic') and
         ((case == 'baseline') == (prior_argv is None)), 'DEV_CASE')
    scope = DescendantScope()
    backend = PreparedNativeBackend(str(root/case),DECODER,FIXTURE_SHA,'d'*64,
        output_factory=OutputDirectory,encode_json=encode_json,parse_listing=parse_listing)
    child = None; captured = False; stdout = stderr = None
    r = dict(case=case,status='refused',pid=None,exit_code=None,reaped=False,
        kill_attempted=False,cleanup_failed=False,capture_stable=False,backend_closed=False,
        scope=None,argv=None,environment=dict(ENV),reason=None,fixture_sha256=FIXTURE_SHA,
        instructions=None,loader=None,qualification_admitted=False,formal_observations=0)
    if case == 'diagnostic': r['environment']['LD_DEBUG']='libs'
    def fail(code):
        r['status']='refused'
        if r['reason'] is None: r['reason']=code
    try:
        scope.activate()
        backend.reserve(); backend.write('target.bin',FIXTURE)
        child = backend.launch(FIXTURE) if case == 'baseline' else diagnostic_launch(backend,prior_argv)
        r['pid']=child.process.pid; r['argv']=list(child.process.args)
        r['exit_code']=child.wait(timeout=3); r['reaped']=True
        captured=True; stdout,stderr=child.capture(CAPTURE_CAP)
        need(len(stdout) < CAPTURE_CAP and len(stderr) < CAPTURE_CAP, 'DEV_CAPTURE_LIMIT')
        backend.write('stdout.bin',stdout); backend.write('stderr.bin',stderr)
        r['capture_stable']=True
        need(type(r['exit_code']) is int and r['exit_code']==0, 'DEV_CHILD_EXIT')
        r['instructions']=fixture_listing(backend.parse(stdout,FIXTURE))
        if case == 'baseline': need(stderr == b'', 'DEV_BASELINE_STDERR')
        else: r['loader']=loader_events(stderr,r['pid'])
        r['status']='decoder_case_complete_unreviewed'
    except BaseException as error:
        r['failure_type']=type(error).__name__
        code=error.args[0] if len(error.args)==1 else None
        r['failure_code']=code if type(code) is str and re.fullmatch(r'[A-Z_]{1,64}',code) else None
        fail('DEV_CASE_REFUSED')
    finally:
        if child is not None:
            try:
                if not child.reaped:
                    code=child.poll()
                    if code is None:
                        r['kill_attempted']=True
                        try: child.kill()
                        except BaseException: r['cleanup_failed']=True; fail('DEV_KILL')
                        code=child.wait(timeout=1)
                    r['exit_code']=code
                r['reaped']=child.reaped
            except BaseException: r['cleanup_failed']=True; fail('DEV_REAP')
            try:
                if child.reaped and not captured:
                    captured=True; stdout,stderr=child.capture(CAPTURE_CAP)
                    backend.write('stdout.bin',stdout); backend.write('stderr.bin',stderr)
                    r['capture_stable']=True
            except BaseException: fail('DEV_CAPTURE')
            try: child.close()
            except BaseException: r['cleanup_failed']=True; fail('DEV_CHILD_CLOSE')
        if scope.active:
            try:
                r['scope']=scope.drain()
                need(r['scope']['status']=='descendant_scope_complete_unreviewed','DEV_SCOPE')
                scope.assert_empty()
            except BaseException: r['cleanup_failed']=True; fail('DEV_SCOPE')
        if backend.directory is not None:
            try: backend.candidate(r)
            except BaseException: fail('DEV_CANDIDATE')
        try: backend.close(); r['backend_closed']=True
        except BaseException: r['cleanup_failed']=True; fail('DEV_CLOSE')
        save_new(root/(case+'-result.json'),r)
    need(r['status']=='decoder_case_complete_unreviewed' and r['reaped'] and
         r['backend_closed'] and not r['cleanup_failed'], 'DEV_STOPPED_NO_RETRY')
    return r,stdout


def graph_identity(graph):
    return {r['path']:dict(bytes=r['bytes'],sha256=r['sha256']) for r in graph['nodes']}


def run(out, plan):
    need(sys.platform=='linux' and list(sys.version_info[:3])==[3,12,3], 'DEV_HOST')
    need(not out.exists() and out.parent.resolve(strict=True)==out.parent, 'DEV_NEW_OUTPUT')
    out.mkdir(mode=0o700)
    save_new(out/'started.json',dict(plan=plan['schema'],decoder_limit=2,formal_observations=0))
    try:
        before=collect(os.path.realpath(sys.executable),DECODER)
        save_new(out/'toolchain-before.json',before)
        need(graph_identity(before)==plan['toolchain_files'] and before['hooks_absent'], 'DEV_TOOLCHAIN')
        baseline,first=run_case(out,'baseline')
        diagnostic,second=run_case(out,'diagnostic',baseline['argv'])
        need(first==second and baseline['instructions']==diagnostic['instructions'], 'DEV_OUTPUT_DIFFERENCE')
        loaded=join_libraries(diagnostic['loader'],before)
        after=collect(os.path.realpath(sys.executable),DECODER)
        save_new(out/'toolchain-after.json',after)
        need(before==after, 'DEV_TOOLCHAIN_DRIFT')
        result=dict(status='DECODER_LOADING_EVIDENCE_NOT_QUALIFIED',decoder_processes=2,
            baseline_backend_exercised=True,diagnostic_only_environment_delta={'LD_DEBUG':'libs'},
            fixture_sha256=FIXTURE_SHA,fixture_bytes=103,instructions=103,
            byte_identical_stdout=True,stdout_sha256=hashlib.sha256(first).hexdigest(),
            loader_initialized_files=loaded,static_and_diagnostic_closure_agree=True,
            toolchain_unchanged=True,production_loader_maps_observed=False,
            original_image_reads=0,formal_preparations=0,formal_observations=0,
            writer_domain_qualified=False,bootstrap_qualified=False,clears_hold=False)
        save_new(out/'completed.json',result)
        print('PASS: fixed synthetic decoder pair, no formal observation')
    except BaseException:
        save_new(out/'refused.json',dict(status='DEVELOPMENT_INCOMPLETE',retry=False,formal_observations=0))
        raise


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('Usage: SCRIPT NEW_DIRECTORY')
    plan_path=Path(__file__).resolve().parents[2]/'docs/20261016-017-bootstrap'/PLAN_NAME
    try: run(Path(sys.argv[1]).absolute(),json.loads(plan_path.read_bytes()))
    except BaseException:
        print('DECODER_DEVELOPMENT_REFUSED',file=sys.stderr)
        raise SystemExit(1)
