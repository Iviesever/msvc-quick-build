"""Exact -I -S -B owner entry. No synthetic mode or live allowlist generator.

The separately reviewed controller pins this script and interpreter before use.
Its trusted source directory is the only project path added for initial preload;
-I alone is not a sandbox. All loaded modules must match the frozen request.
"""
from pathlib import Path
import os
import sys

if __name__ == '__main__':
    if not (sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode and not sys.flags.optimize):
        raise SystemExit('BOOTSTRAP_ISOLATION_REQUIRED')
    sys.path.insert(0,str(Path(__file__).resolve().parent))

from bcrypt_bootstrap_plan_017 import BootstrapInputs, BOOTSTRAP_NAME
from bcrypt_bound_observation_017 import BoundObservation
from bcrypt_admission_contract_017 import encode, decode, equal, REPORT_CAP
from bcrypt_guarded_owner_017 import need
from bcrypt_owner_backend_017 import PreparedNativeBackend


def run_owner(inputs):
    """Use fixed legacy dependencies; no replaceable verifier or owner factory."""
    need(type(inputs) is BootstrapInputs, 'BOOT_INPUT_TYPE')
    # Only two fixed imports, before ImportBoundary activation. Failures never launch.
    from bcrypt_disassembly_io_014 import OutputDirectory
    from bcrypt_disassembly_listing_014 import parse_listing
    inputs.verify_control_sources()
    c,p = inputs.request['context'],inputs.plan
    need(os.path.realpath(__file__) == p['bootstrap']['path'] and
         Path(__file__).name == BOOTSTRAP_NAME, 'BOOT_SOURCE')
    gate = BoundObservation(inputs.request_raw,p['request']['sha256'],
                            metadata=inputs.metadata,receipts=inputs.receipts)
    backend = PreparedNativeBackend(c['output_dir'],c['programs']['decoder'],
        c['target']['sha256'],p['request']['sha256'],output_factory=OutputDirectory,
        encode_json=encode,parse_listing=parse_listing)
    raw = gate.run(backend,inputs.primary,inputs.independent)
    value = decode(raw,REPORT_CAP)
    equal(value['binding'],inputs.binding)
    # The outer process must observe this exit; JSON status alone is insufficient.
    return raw, 0 if value['status'] == 'owner_complete_unreviewed' else 1


def main(argv):
    try:
        need(type(argv) is list and len(argv) == 3, 'BOOT_ARGV')
        need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode and
             not sys.flags.optimize and os.path.realpath(argv[0]) == os.path.realpath(__file__),
             'BOOT_ISOLATION')
        inputs = BootstrapInputs(argv[1],argv[2])
        raw, code = run_owner(inputs)
        need(os.write(1,raw) == len(raw), 'BOOT_STDOUT')
        return code
    except BaseException:
        # Preloaded, fixed-size diagnostic. Do not serialize paths or exception text.
        try: os.write(2,b'BOOTSTRAP_REFUSED\n')
        except BaseException: pass
        return 1


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
