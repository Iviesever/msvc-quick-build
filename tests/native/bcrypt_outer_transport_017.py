"""One bounded containing-process transport; NOT a writer-quiescence proof.

The exact launch spec digest is supplied out of band. This component neither
constructs that authority nor loads the 103-byte target. It captures owner-final
bytes over pipes, records the containing exit, and retains exclusive files.
A decoder started in another session is not contained by killing this owner.
Therefore this receipt NEVER substitutes for bcrypt-outer-completion-017-v1.
"""
from __future__ import annotations
import os
from pathlib import Path
import selectors
import signal
import stat
import subprocess
import time

from bcrypt_admission_contract_017 import encode, decode, fields, fingerprint
from bcrypt_guarded_owner_017 import Refused, need
from bcrypt_owner_backend_017 import ENV, absolute, sha256_text

SPEC_CAP, FINAL_CAP, STDERR_CAP, EXIT_CAP = 32768, 65536, 65, 4096
CAPS = {'launch.json':SPEC_CAP, 'owner-final.json':FINAL_CAP,
        'owner-stderr.bin':STDERR_CAP, 'process-exit.json':EXIT_CAP}


def validate_spec(raw, expected_sha):
    sha256_text(expected_sha)
    need(type(raw) is bytes and 0 < len(raw) <= SPEC_CAP, 'TRANSPORT_SPEC')
    need(fingerprint(raw)['sha256'] == expected_sha, 'TRANSPORT_ROOT')
    spec=decode(raw,SPEC_CAP)
    fields(spec,('schema','request_sha256','argv','cwd','output_dir','environment','wall_ms'))
    need(spec['schema']=='bcrypt-owner-launch-017-v1','TRANSPORT_SPEC')
    sha256_text(spec['request_sha256']); absolute(spec['cwd']); absolute(spec['output_dir'])
    argv=spec['argv']
    need(type(argv) is list and 1 <= len(argv) <= 24,'TRANSPORT_ARGV')
    absolute(argv[0])
    need(all(type(s) is str and 0 < len(s) <= 4096 and '\0' not in s for s in argv), 'TRANSPORT_ARGV')
    need(sum(len(s) for s in argv) <= 8192 and spec['environment']==ENV, 'TRANSPORT_SPEC')
    need(type(spec['wall_ms']) is int and 1 <= spec['wall_ms'] <= 16000,'TRANSPORT_SPEC')
    return spec


def _stamp(st):
    return (st.st_dev,st.st_ino,st.st_mode,st.st_nlink,st.st_size,st.st_mtime_ns,st.st_ctime_ns)


class RetainedTransport:
    """Fresh, local four-file directory. Never deletes or overwrites a prefix."""
    def __init__(self,path):
        self.path=Path(absolute(path)); self.fd=None; self.closed=False
        need(str(self.path.parent.resolve(strict=True))==str(self.path.parent),'TRANSPORT_PATH')
        parent=os.open(self.path.parent,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
        try:
            os.mkdir(self.path.name,mode=0o700,dir_fd=parent)
            self.fd=os.open(self.path.name,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=parent)
            self.identity=_stamp(os.fstat(self.fd))[:4]
            os.fsync(parent)
        except BaseException:
            if self.fd is not None: os.close(self.fd); self.fd=None
            raise
        finally: os.close(parent)

    def check(self):
        need(not self.closed and self.fd is not None,'TRANSPORT_CLOSED')
        need(str(self.path.resolve(strict=True))==str(self.path) and
             _stamp(os.fstat(self.fd))[:4]==self.identity==_stamp(self.path.lstat())[:4], 'TRANSPORT_PATH')

    def create(self,name,raw):
        self.check()
        need(name in CAPS and type(raw) is bytes and len(raw)<=CAPS[name],'TRANSPORT_SIZE')
        fd=os.open(name,os.O_RDWR|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=self.fd)
        try:
            before=os.fstat(fd); need(stat.S_ISREG(before.st_mode) and before.st_nlink==1,'TRANSPORT_FILE')
            cursor=0
            while cursor<len(raw):
                n=os.write(fd,raw[cursor:]); need(n>0,'TRANSPORT_WRITE'); cursor+=n
            os.fsync(fd)
            need(os.pread(fd,len(raw)+1,0)==raw,'TRANSPORT_READBACK')
            need(_stamp(os.fstat(fd))==_stamp(os.stat(name,dir_fd=self.fd,follow_symlinks=False)), 'TRANSPORT_FILE')
        finally: os.close(fd)
        os.fsync(self.fd); self.check()

    def close(self):
        need(not self.closed,'TRANSPORT_CLOSED'); self.closed=True
        if self.fd is not None:
            fd,self.fd=self.fd,None; os.close(fd)


def _stop_root(process,record):
    """Only an owned, unreaped direct child; no speculative process-group kill."""
    if process is None or record['owner_reaped']: return
    try:
        code=process.poll()
        if code is None:
            record['kill_attempted']=True
            try: process.kill()
            except ProcessLookupError: pass
            except BaseException: record['cleanup_failed']=True
            code=process.wait(timeout=1)
        need(type(code) is int,'TRANSPORT_REAP')
        record.update(owner_reaped=True,owner_exit_code=code)
    except BaseException:
        record['cleanup_failed']=True


class OwnerTransport:
    """Single use. Real Popen/select/read/wait; no mutable injected launcher.

    Popen and filesystem system calls are not hard-interruptible. The deadline
    covers pipe collection and ordinary wait. At most one extra 1s cleanup wait.
    The fixed accepted stdout/stderr caps bound retained memory and file bytes;
    one extra byte detects overflow. Pipes use bounded reads, never communicate.
    """
    def __init__(self,raw,expected_sha):
        self.spec=validate_spec(raw,expected_sha)
        self.raw,self.sha,self.used=raw,expected_sha,False

    def run(self):
        need(not self.used,'REUSED'); self.used=True
        # Revalidate the retained bytes. A caller's mutation of self.spec is not used.
        s=validate_spec(self.raw,self.sha)
        need(os.name=='posix' and signal.getsignal(signal.SIGCHLD)==signal.SIG_DFL,'TRANSPORT_PLATFORM')
        start=time.monotonic_ns(); deadline=start+s['wall_ms']*1_000_000
        process=out=selector=None; buffers={'stdout':bytearray(),'stderr':bytearray()}
        r=dict(schema='bcrypt-owner-transport-017-v1',launch_sha256=self.sha,
               request_sha256=s['request_sha256'],status='refused',reason=None,
               launch_reserved=False,launch_outcome_unknown=False,owner_reaped=False,owner_exit_code=None,
               timed_out=False,interrupted=False,kill_attempted=False,cleanup_failed=False,
               pipes_eof=False,capture_complete=False,final_json=False,readback_verified=False,
               persistence_failed=False,output_closed=False,elapsed_ns=None,
               writers_quiescent=False,observation_accepted=False,clears_hold=False)
        def fail(code):
            if r['reason'] is None: r['reason']=code
            r['status']='refused'
        def remaining():
            value=deadline-time.monotonic_ns()
            if value<=0: raise TimeoutError()
            return value/1_000_000_000
        try:
            out=RetainedTransport(s['output_dir']); out.create('launch.json',self.raw)
            remaining(); r['launch_reserved']=True
            process=subprocess.Popen(s['argv'],cwd=s['cwd'],env=dict(ENV),stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0,shell=False,
                close_fds=True,start_new_session=True,restore_signals=True)
            selector=selectors.DefaultSelector()
            for name,stream in [('stdout',process.stdout),('stderr',process.stderr)]:
                os.set_blocking(stream.fileno(),False); selector.register(stream,selectors.EVENT_READ,name)
            while selector.get_map():
                for key,_ in selector.select(remaining()):
                    name=key.data; cap=FINAL_CAP if name=='stdout' else STDERR_CAP
                    data=os.read(key.fd,min(4096,cap-len(buffers[name])+1))
                    if not data: selector.unregister(key.fileobj); continue
                    room=cap-len(buffers[name]); buffers[name].extend(data[:room])
                    need(len(data)<=room,'TRANSPORT_OVERFLOW')
            r['pipes_eof']=True
            code=process.wait(timeout=remaining()); need(type(code) is int,'TRANSPORT_REAP')
            r.update(owner_reaped=True,owner_exit_code=code,capture_complete=True)
            remaining()
            need(code==0,'TRANSPORT_EXIT'); need(not buffers['stderr'],'TRANSPORT_STDERR')
            final=decode(bytes(buffers['stdout']),FINAL_CAP)
            need(type(final) is dict and final.get('status')=='owner_complete_unreviewed' and
                 type(final.get('binding')) is dict and
                 final['binding'].get('request_sha256')==s['request_sha256'],'TRANSPORT_FINAL')
            r['final_json']=True; remaining(); r['status']='owner_transport_complete_unreviewed'
        except (TimeoutError,subprocess.TimeoutExpired):
            r['timed_out']=True; fail('TRANSPORT_DEADLINE')
        except (KeyboardInterrupt,SystemExit):
            r['interrupted']=True; fail('INTERRUPTED')
        except BaseException as error:
            code=error.args[0] if isinstance(error,Refused) and error.args else 'TRANSPORT_IO'
            fail(code if type(code) is str and code.startswith('TRANSPORT_') else 'TRANSPORT_IO')
        finally:
            if r['launch_reserved'] and process is None: r['launch_outcome_unknown']=True
            _stop_root(process,r)
            if r['cleanup_failed']: fail('TRANSPORT_CLEANUP')
            if selector is not None:
                try: selector.close()
                except BaseException: r['cleanup_failed']=True; fail('TRANSPORT_CLEANUP')
            if process is not None:
                for stream in (process.stdout,process.stderr):
                    try: stream.close()
                    except BaseException: r['cleanup_failed']=True; fail('TRANSPORT_CLEANUP')
            if out is not None:
                try:
                    out.create('owner-final.json',bytes(buffers['stdout']))
                    out.create('owner-stderr.bin',bytes(buffers['stderr']))
                    r['readback_verified']=True
                    r['elapsed_ns']=time.monotonic_ns()-start
                    if r['elapsed_ns']>=s['wall_ms']*1_000_000:
                        r['timed_out']=True; fail('TRANSPORT_DEADLINE')
                    out.create('process-exit.json',encode(r,EXIT_CAP))
                except BaseException:
                    r['persistence_failed']=True; fail('TRANSPORT_PERSISTENCE')
                try: out.close(); r['output_closed']=True
                except BaseException: r['persistence_failed']=True; fail('TRANSPORT_CLOSE')
            r['elapsed_ns']=time.monotonic_ns()-start
            if r['elapsed_ns']>=s['wall_ms']*1_000_000:
                r['timed_out']=True; fail('TRANSPORT_DEADLINE')
        return r
