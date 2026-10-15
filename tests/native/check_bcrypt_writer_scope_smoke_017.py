"""Explicit five-case Linux development registration; never unittest discovery.

One fresh supervisor, exactly five Popen roots and five bounded fork children.
No GNU, loader probe, original image, actual target or formal 017 observation.
Run only once against a new output directory after the source/argv registration.
"""
from pathlib import Path
import os
import sys

sys.path.insert(0,str(Path(__file__).resolve().parent))
from bcrypt_outer_transport_017 import ENV, encode, fingerprint
from bcrypt_scoped_transport_017 import ScopedOwnerTransport

FINAL = encode(dict(status='owner_complete_unreviewed',
                    binding=dict(request_sha256='a'*64))).decode('ascii')


def program(case, marker):
    prefix='import os,time\n'
    writer=("os.setsid()\nos.close(1)\nos.close(2)\ntime.sleep(3)\n"
            f"open({str(marker)!r},'wb').write(b'unexpected surviving writer')\nos._exit(0)\n")
    if case == 'clean': return prefix+f'print({FINAL!r},end="")\n'
    if case == 'joined':
        return prefix+"pid=os.fork()\nif pid==0:\n os.setsid()\n os._exit(0)\nos.waitpid(pid,0)\n"+f'print({FINAL!r},end="")\n'
    if case == 'double':
        body="os.setsid()\nchild=os.fork()\nif child==0:\n"+'\n'.join(' '+s for s in writer.replace('os.setsid()\n','').splitlines())+"\nos._exit(0)\n"
        return prefix+"pid=os.fork()\nif pid==0:\n"+'\n'.join(' '+s for s in body.splitlines())+f'\nos.waitpid(pid,0)\nprint({FINAL!r},end="")\n'
    if case not in ('orphan','timeout'): raise ValueError('Unknown smoke case')
    body=prefix+'pid=os.fork()\nif pid==0:\n'+'\n'.join(' '+s for s in writer.splitlines())+'\n'
    return body+('time.sleep(3)\n' if case=='timeout' else f'print({FINAL!r},end="")\n')


def save(path,value):
    data=encode(value)
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:
        if os.write(fd,data)!=len(data):raise OSError('Short development record write')
        os.fsync(fd)
    finally:os.close(fd)
    if path.read_bytes()!=data:raise ValueError('Development record readback')


def run(root):
    if root.exists():raise ValueError('New directory required; never resume')
    root.mkdir(mode=0o700)
    rows=[]
    for case in ('clean','joined','orphan','double','timeout'):
        marker=root/(case+'-unexpected.bin')
        text=program(case,marker)
        spec=dict(schema='bcrypt-owner-launch-017-v1',request_sha256='a'*64,
            argv=[os.path.realpath(sys.executable),'-I','-S','-B','-c',text],cwd=str(root),
            output_dir=str(root/case),environment=dict(ENV),wall_ms=200 if case=='timeout' else 1000)
        raw=encode(spec);wrapper=ScopedOwnerTransport(raw,fingerprint(raw)['sha256'])
        report=wrapper.run();save(root/(case+'-scope.json'),report)
        scope=report['scope'];direct=report['transport']
        good=scope['descendant_domain_empty'] and scope['policy_restored'] and not scope['cleanup_failed']
        if case in ('clean','joined'):
            good=good and report['status']=='scoped_transport_complete_unreviewed' and scope['children_reaped']==0
        else:
            good=good and report['status']=='refused' and scope['reason']=='SCOPE_ORPHANS' and scope['children_reaped']==1
            good=good and scope['signal_attempts']==1 and scope['exit_codes']==[-9]
            good=good and (direct['timed_out'] if case=='timeout' else direct['status']=='owner_transport_complete_unreviewed')
        good=good and not marker.exists()
        if case=='clean' and good:
            backend=root/'synthetic-backend';backend.mkdir()
            for name,raw in {'consumed.json':b'{}\n','target.bin':bytes(range(103)),
                  'tool-stdout.bin':b'synthetic not decoded\n','tool-stderr.bin':b'','report.json':b'{}\n'}.items():
                (backend/name).write_bytes(raw)
            files,hashes=wrapper.read_artifacts(str(backend))
            good=len(files)==6 and files['owner-final.json']==FINAL.encode()
            save(root/'synthetic-readback.json',dict(hashes=hashes,readback=True,
                 evaluated_completion=False,qualification=False))
        rows.append(dict(case=case,passed=bool(good),scope_reason=scope['reason'],
            transport_reason=direct['reason'],reaped=scope['children_reaped'],signals=scope['signal_attempts']))
        save(root/(case+'-verdict.json'),rows[-1])
        if not good:raise RuntimeError('First failing smoke case: '+case)
    save(root/'results.json',dict(cases=rows,popen_roots=5,explicit_fork_children=5,
         supervisor_runs=1,formal_observations=0,original_image_reads=0,new_gnu=0))
    print('PASS: five cases; five Popen roots plus five fork children; zero formal observations')


if __name__=='__main__':
    if not (sys.flags.isolated and sys.flags.no_site):
        raise SystemExit('Dedicated supervisor requires -I -S; no relaxed thread check')
    if len(sys.argv)!=2:raise SystemExit('Usage: check_bcrypt_writer_scope_smoke_017.py NEW_DIRECTORY')
    run(Path(sys.argv[1]).absolute())
