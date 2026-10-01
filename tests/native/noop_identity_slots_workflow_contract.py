"""Exact new manual workflow extension; retain all 50 historical workflow pins."""
import hashlib

WORKFLOW = '.github/workflows/noop-identity-slots-study.yml'
WORKFLOW_SHA256 = '73425e14a4e523deaef53d47aa2fcebf842ddb606cfe7f4eb210413c6c388e0f'
OLD_WORKFLOW_SHA256 = '2e928f795bff8a8148f4e3f7f40eb3ac869f7dc0f0ddf859ea1827a3fd560a3c'
ENTRY_PINS = {'tests/native/noop_identity_slots.py': 'ea5be4fe8a54124358b484f0430ee748c98b317d5be54d728556e560a50411e0', 'tests/native/noop_identity_slots_runtime.psm1': 'a890e0419df32d36ed37ea25ec36a1d2ccac8de083c4cf16f6560a20367f3783', 'tests/native/external_noop_boundary.py': 'daad65d07b52edda2fa5a3f07c53c8c123b357a8bfe09039ffb151c43a45fee9', 'tests/native/noop_identity_slots_capture.psm1': '4c71fe64671c89459b8007bf51d22414f01447a3761e807af5429c706b250d8a', 'tests/native/run_noop_identity_slots.ps1': 'ed1a3999e3cf61a710bd272ffb0bdc6c65a1367f4a80becc0f062469a2f7f112'}


def legacy_workflow_view(values):
    """Exclude only the exact, separately tested manual addition; reject all drift."""
    if values.get(WORKFLOW) != WORKFLOW_SHA256:
        raise ValueError('Missing or changed manual slot workflow')
    old = {name: digest for name, digest in values.items() if name != WORKFLOW}
    fingerprint = ''.join(f'{name}\0{digest}\n' for name, digest in sorted(old.items()))
    if len(old) != 50 or hashlib.sha256(fingerprint.encode()).hexdigest() != OLD_WORKFLOW_SHA256:
        raise ValueError('Historical workflow inventory changed')
    return old
