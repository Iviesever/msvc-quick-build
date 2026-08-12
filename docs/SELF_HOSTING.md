# MQB stable self-hosting contract

A stable MQB release must be able to build MQB itself without consulting `CMakeLists.txt` for the self-host build.

This is a release-blocking contract, not an optional demonstration.

## What "self-hosted release" means

The release workflow uses three logical stages:

1. **Stage 0 — bootstrap/test binary**
   - built by CMake on the Windows CI runner;
   - used to run the full Release CTest suite;
   - used only to start the self-host chain;
   - **never packaged or published**.
2. **Stage 1 — self-hosted release candidate**
   - built by Stage 0 by invoking MQB on `cpp/apps/mqb/main.cpp`;
   - build policy and production source manifest come from `cpp/mqb.json`;
   - `CMakeLists.txt` is not read or invoked for this build;
   - this is the `mqb.exe` placed in the stable ZIP.
3. **Stage 2 — closure proof**
   - after Stage 1 is copied outside the project build state, `cpp/.mqb` is deleted;
   - Stage 1 then builds MQB again from the same source/config from a clean MQB state;
   - Stage 2 must report the same release version.

```text
bootstrap Stage 0
      |
      | mqb + cpp/mqb.json
      v
self-hosted Stage 1  ---> stable package mqb.exe
      |
      | delete cpp/.mqb, then mqb + cpp/mqb.json
      v
Stage 2 closure proof
```

The stable package is rejected if Stage 0 cannot build Stage 1, if Stage 1 cannot build Stage 2, or if either self-hosted binary reports the wrong embedded version.

## Native project description

`cpp/mqb.json` is the native project description for building MQB with MQB. It declares:

- Release configuration;
- x64 architecture;
- C++23;
- executable target;
- static `MT` runtime;
- console subsystem;
- MQB's production include roots;
- `/W4` and `/permissive-`;
- the complete production translation-unit set outside tests.

`tests/selfhost/selfhost.ps1` independently enumerates the production source directories and requires the `mqb.json` entry source plus `discovery.extra_sources` to exactly cover that set. Adding or removing a production `.cpp` without updating the native self-host manifest fails the gate.

The release version remains sourced from `release/VERSION`. The self-host harness supplies it as the structured `MQB_VERSION` preprocessor definition, so `cpp/mqb.json` does not duplicate the release number.

## Manual self-build

Given an MQB executable and a checkout of the matching source tag, a self-build can be performed without CMake:

```powershell
Set-Location cpp
$version = (Get-Content ..\release\VERSION -Raw).Trim()
$quote = [char]34
$define = 'MQB_VERSION=' + $quote + $version + $quote

& C:\path\to\mqb.exe apps\mqb\main.cpp --env vs -D $define
```

The native self-host result is:

```text
cpp\.mqb\bin\mqb.exe
```

The literal quote characters in `$define` are intentional. Do not pre-escape them with backslashes: MQB owns Windows argv encoding and passes the structured definition to `cl.exe`.

Running the result with `--help` must report the same version. Running the same command again with that newly built executable from a clean `cpp/.mqb` state is the closure proof used by CI.

## Release artifact rule

The stable ZIP must contain the Stage 1 binary, not the Stage 0 bootstrap binary.

Before upload, the release workflow verifies that the ZIP's `mqb.exe` is byte-identical to the validated Stage 1 executable, then runs the packaged install/reinstall/uninstall lifecycle against that exact ZIP.

CMake remains useful as a development/test bootstrap and for the unit/integration test graph, but it is not the producer of the `mqb.exe` shipped in a stable release.
