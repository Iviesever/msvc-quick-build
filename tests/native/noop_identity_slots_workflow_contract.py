"""Exact new manual workflow extension; retain all 50 historical workflow pins."""
import hashlib

WORKFLOW = '.github/workflows/noop-identity-slots-study.yml'
WORKFLOW_SHA256 = '3650d9fa248b506e8db07dc80ac431c6c54d55950041e5c1d45deaf7a3a4fb8e'
OLD_WORKFLOW_SHA256 = '2e928f795bff8a8148f4e3f7f40eb3ac869f7dc0f0ddf859ea1827a3fd560a3c'
ENTRY_PINS = {'tests/native/noop_identity_slots.py': 'ea5be4fe8a54124358b484f0430ee748c98b317d5be54d728556e560a50411e0', 'tests/native/noop_identity_slots_runtime.psm1': 'e0e1f4880c2b948ff82c59d463d53e0c5bc9d5d4acc9d6ff5be470d6475f0926', 'tests/native/external_noop_boundary.py': 'daad65d07b52edda2fa5a3f07c53c8c123b357a8bfe09039ffb151c43a45fee9', 'tests/native/noop_identity_slots_capture.psm1': '4c71fe64671c89459b8007bf51d22414f01447a3761e807af5429c706b250d8a', 'tests/native/run_noop_identity_slots.ps1': 'e76b206cf96113746d8b409c9001fde396251560fb2a6b4e2d2245041a7fa2f3'}

REPORTING = '.github/workflows/reporting-journal-correctness.yml'
REPORTING_OLD_SHA256 = '740234651ec4f4434d30cb8b054f5df68679dd95f55dee99feaaad6adc5b7a62'
REPORTING_SHA256 = '323446939010cc5265785a72c5a1b458d38aad0d78c858c7da42c6da0ed3f9b5'


def legacy_workflow_view(values):
    """Exclude only the exact, separately tested manual addition; reject all drift."""
    if values.get(WORKFLOW) != WORKFLOW_SHA256:
        raise ValueError('Missing or changed manual slot workflow')
    old = {name: digest for name, digest in values.items() if name != WORKFLOW}
    if old.get(REPORTING) != REPORTING_SHA256:
        raise ValueError('Changed fixed Reporting fixture extension')
    old[REPORTING] = REPORTING_OLD_SHA256
    fingerprint = ''.join(f'{name}\0{digest}\n' for name, digest in sorted(old.items()))
    if len(old) != 50 or hashlib.sha256(fingerprint.encode()).hexdigest() != OLD_WORKFLOW_SHA256:
        raise ValueError('Historical workflow inventory changed')
    return old
