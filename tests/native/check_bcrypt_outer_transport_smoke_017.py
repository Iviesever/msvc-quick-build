"""Explicit four-child development smoke; never a formal 017 observation.

Not named test_*.py: unittest discovery does not launch these child processes.
Each case runs only the current Python with a short literal -c program. No
original image, metadata, decoder, module inventory or actual target is opened.
"""
import argparse
from pathlib import Path
import sys

import bcrypt_outer_transport_017 as t

PAYLOAD=t.encode({'status':'owner_complete_unreviewed','binding':{'request_sha256':'a'*64}})
CASES=(
    ('clean', 'import os;os.write(1,'+repr(PAYLOAD)+')', 2000, None),
    ('exit37', 'import os;os.write(1,'+repr(PAYLOAD)+');raise SystemExit(37)', 2000, 'TRANSPORT_EXIT'),
    ('timeout', 'import time;time.sleep(5)', 200, 'TRANSPORT_DEADLINE'),
    ('overflow', 'import os;os.write(1,b"x"*65537)', 2000, 'TRANSPORT_OVERFLOW'),
)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--expected-source-sha256',required=True)
    args=parser.parse_args()
    source=Path(t.__file__).read_bytes()
    t.need(t.fingerprint(source)['sha256']==args.expected_source_sha256,'SOURCE_MISMATCH')
    root=args.output.absolute();root.mkdir(mode=0o700,exist_ok=False)
    results=[]
    for name,code,wall,reason in CASES:
        spec=dict(schema='bcrypt-owner-launch-017-v1',request_sha256='a'*64,
                  argv=[str(Path(sys.executable).resolve()),'-I','-S','-B','-c',code],
                  cwd=str(root),output_dir=str(root/name),environment=dict(t.ENV),wall_ms=wall)
        raw=t.encode(spec)
        owner=t.OwnerTransport(raw,t.fingerprint(raw)['sha256'])
        r=owner.run()
        t.need(r['reason']==reason and r['owner_reaped'] and r['output_closed'],'SMOKE_FAILED')
        t.need(r['writers_quiescent'] is False and r['observation_accepted'] is False,'FALSE_PROOF')
        if name=='clean':
            t.need(r['owner_exit_code']==0 and r['capture_complete'] and r['final_json'],'SMOKE_FAILED')
            t.need((root/name/'owner-final.json').read_bytes()==PAYLOAD,'SMOKE_BYTES')
        else:
            t.need(r['status']=='refused','FALSE_SUCCESS')
        if name=='exit37':t.need(r['owner_exit_code']==37,'SMOKE_EXIT')
        if name=='timeout':t.need(r['timed_out'] and r['kill_attempted'],'SMOKE_TIMEOUT')
        if name=='overflow':t.need((root/name/'owner-final.json').stat().st_size==65536,'SMOKE_CAP')
        try:owner.run()
        except t.Refused as exc:t.need(str(exc)=='REUSED','SMOKE_REUSE')
        else:raise AssertionError('second launch allowed')
        r['case']=name;results.append(r)
        # A return record is separate from the immutable pre-close exit candidate.
        with (root/(name+'-return.json')).open('xb') as f:f.write(t.encode(r,t.EXIT_CAP))
        print(name,r['status'],r['owner_exit_code'],flush=True)
    result=dict(schema='bcrypt-transport-smoke-017-v1',source_sha256=args.expected_source_sha256,
                actual_python_children=4,formal_observations=0,gnu_invocations=0,
                original_image_reads=0,all_cases_passed=True,results=results)
    with (root/'results.json').open('xb') as f:f.write(t.encode(result))


if __name__=='__main__':main()
