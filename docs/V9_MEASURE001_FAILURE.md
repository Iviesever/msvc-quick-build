# Consumed measurement001: validator path correction (no retry)

The first measurement, run36107949309/attempt1, stopped after one successful
baseline prime, before the first no-op. Its original eight-file metadata artifact
10851277845 (5303 bytes, SHA256
`c0ac5b61a9e3e0ca7956794cf221a1dd4c2bb13a10ceeb916f58f01f14a9c30b`)
is retained as the small, hash-pinned regression fixture
`tests/native/v9_noop_validation_failed_measurement.zip`. It contains no EXEs,
cache contents or environment values. No stopped result is rewritten as success.

`Application.cpp` explicitly sets `DiscoveryOptions.cache_file` to the project
layout's `vs-<architecture>.cache`. Only callers without an explicit cache file
use the locator's `msvc-<preference>-<host>-<target>.mqbcache` fallback. The original
PowerShell projection and Python validator incorrectly assumed that fallback.
The saved prime manifest records the actual CLI file, so this is a validator
wiring defect, not a cache-writing failure or a measured product regression.

Both validator paths now target the CLI destination. Regression tests use the
original failed prefix independently of the Python constant, bind the CLI source
assignment, reject a fallback-only fixture, and let the simulated call loop write
real synthetic cache files for the actual projection function. The projection is
no longer replaced by a same-assumption test double. The original stopped
completion must still fail before the gate evaluator; accepting the individual
prime's shape does not create the fifteen missing calls.

There are zero no-op samples/pairs. Recorded study calls are now **121** (120+1
prime); preparation's five calls remain separate. The other fifteen slots are
unused, not permission to refill. No benefit/no-benefit/regression verdict on the
production change follows from this stopped run. No product, prepared binaries,
threshold, protocol budget, allocation label or manual workflow changes here.
Both original opportunities remain consumed. **Do not rerun run1/run2 or dispatch
another run.** A future measurement needs a separately reviewed protocol that
preserves this failure and the exact prepared inputs; this correction grants no
new opportunity and does not solve that future admission boundary implicitly.
