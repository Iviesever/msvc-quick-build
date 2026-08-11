# Incremental + run-argv shared parity

This slice extends the stable-v5 PowerShell ↔ C++ migration campaign with observable behavior that should remain meaningful across the two different build implementations.

## Incremental contract

The parity driver copies the same `fixtures/incremental` source into isolated PowerShell and native C++ sandboxes.

For each implementation it then:

1. performs a cold build and runs the result;
2. records the target executable timestamp;
3. rebuilds without changing inputs and requires the target timestamp to remain unchanged;
4. applies the same source mutation to both sandboxes;
5. rebuilds and requires both programs to expose the same new behavior.

The test intentionally does **not** compare PowerShell cache files with `.mqb/`, object names, or implementation-specific "up-to-date" log strings. The stable contract is that a warm no-op does not rewrite the final target and that a real source change reaches the rebuilt program.

## Run-argv contract

The shared `fixtures/run_argv` program prints its received argv tokens.

The Golden Reference is exercised through its public legacy surface:

```powershell
build.ps1 main.cpp -run -a "alpha beta 42"
```

Native MQB is exercised through structured argv:

```powershell
mqb main.cpp --run -- alpha beta 42
```

Both must expose the shared simple-token result:

```text
argv=alpha|beta|42
```

This does not claim that every historical quoting/escaping quirk of the legacy single-string `-a` tokenizer is part of the native contract. Those edge cases remain an explicit migration decision; native `--` preserves structured argv boundaries by design.

## CI

`.github/workflows/cpp-parity.yml` builds native `mqb.exe` in both Debug and Release configurations and runs the same PowerShell Golden Reference parity driver against each binary. The ordinary CTest suites remain responsible for lower-level and baseline shared-parity coverage; this workflow is the growing cross-implementation migration matrix.
