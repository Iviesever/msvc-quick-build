# Library, architecture, and failure shared parity

This slice extends the stable-v5 PowerShell Golden Reference ↔ native C++ migration matrix without comparing implementation-private cache or artifact metadata.

## Library consumer contract

The same fixture contains a static-library provider and a consumer. Each implementation first builds the provider through its public static-library target surface, then builds the consumer through its public library search/name surface, and finally runs the executable.

The observable contract is:

```text
library=73
```

PowerShell and native MQB intentionally use their own artifact locations (`parity_support.lib` beside the legacy build versus `.mqb/bin/parity_support.lib` for native MQB). Location parity is not required; successful production, resolution, linking, and execution are.

## Architecture contract

The same source is built twice by each implementation:

- x64 must run and report `arch=x64` through `_M_X64`;
- x86 must run and report `arch=x86` through `_M_IX86`.

This validates the user-visible target architecture rather than only checking that the CLI accepted an architecture switch.

## Failure contract

The same syntax-invalid translation unit is built in isolated sandboxes. Both implementations must:

- return a non-zero build status;
- leave no final target executable behind.

The exact numeric exit code and diagnostic wording are implementation details and are not required to match. Stable parity requires a clear failed build with no stale success artifact.

## CI

The existing `Shared PowerShell C++ Parity` workflow runs this driver against both Debug- and Release-built native `mqb.exe` binaries, alongside the earlier incremental/run-argv parity driver. Future config-migration scenarios should extend the same workflow.
