# MQB Self-Hosting and Release Contract

**[简体中文](SELF_HOSTING.md) | English**

This document defines the **bootstrap, validation, and publication boundary** for stable releases. See [`DEVELOPMENT_EN.md`](DEVELOPMENT_EN.md) for the day-to-day contributor workflow.

## 1. Bootstrap seed

MQB builds itself with MQB, so the first binary generation from current source must be started by an already-existing trusted `mqb.exe`.

Stable v5 uses the historical `v5.0.0-rc.2` release binary as the pinned seed. CI verifies the seed Release ZIP SHA-256 and executable identity. The seed only builds **Stage 0** from current source and never enters the final package.

## 2. Bootstrap chain

```text
pinned historical MQB seed
          ↓
      Stage 0
          ↓
  full Release test suite
          ↓
      Stage 1  ─────> mqb.exe used by the stable package
          ↓
    clear MQB build state
          ↓
      Stage 2
```

- **Stage 0** proves that the historical seed can build the current implementation.
- **Stage 1** is built again by the current implementation and becomes the release candidate.
- **Stage 2** is built by Stage 1 after clearing MQB build state, proving clean self-host closure.

All three generations come from the same candidate commit.

## 3. Version source

MQB builds itself from [`cpp/mqb.json`](../cpp/mqb.json), which describes the production source set.

The repository's single release-version source is the root file:

```text
VERSION
```

The build driver injects that value as a structured `MQB_VERSION` definition. The version is not duplicated in `mqb.json` or repository release-note files.

## 4. Release-blocking validation

A stable candidate must prove, on the same candidate commit, that:

1. the pinned seed checksum and executable identity are correct;
2. the seed can build current Stage 0;
3. Stage 0 passes the full Release native test suite;
4. Stage 0 can build Stage 1;
5. after clearing build state, Stage 1 can build Stage 2;
6. Stage 1 and Stage 2 report the correct release version;
7. `mqb.exe` inside the ZIP is byte-identical to the validated Stage 1 binary;
8. the ZIP contains only the runtime/installer payload plus the Apache-2.0 `LICENSE`, with an exact manifest and SHA-256 sidecar;
9. the packaged installer passes install / reinstall / uninstall lifecycle, BAT-wrapper, and PATH-ownership validation.

Any failure blocks publication.

## 5. Stable package

For version `X.Y.Z`, the published assets are:

```text
msvc-quick-build-vX.Y.Z-windows-x64.zip
msvc-quick-build-vX.Y.Z-windows-x64.zip.sha256
```

The next stable ZIP contract on current `main` is a **flat, runtime-only** package containing exactly:

```text
mqb.exe
VERSION
install.bat
install.ps1
uninstall.bat
uninstall.ps1
LICENSE
```

User documentation such as READMEs, architecture, configuration, installation, self-hosting, and release notes is **not shipped inside the ZIP**. It remains in the repository and on GitHub Releases. `LICENSE` remains in the binary redistribution as the required Apache-2.0 license copy.

Already-published Releases/tags and assets remain immutable, so historical v5.1.0 remains the previously validated six-file snapshot. `uninstall.bat` enters the official package starting with the next stable release that contains this mainline change.

The historical seed, Stage 0, Stage 2, tests, and source code are not included either.

See [`INSTALLATION_EN.md`](INSTALLATION_EN.md) for installation behavior.

## 6. Publication

Normal stable publication is driven by a change to the root `VERSION` file:

1. the candidate commit containing the new `VERSION` is merged into `main`;
2. `Native Release` reruns the complete build/test/self-host/package gate on that exact `main` commit;
3. after all gates pass, the workflow creates the `vX.Y.Z` tag and GitHub Release from the already-validated ZIP and checksum;
4. GitHub generates release notes from PR/commit history since the previous tag, so release notes no longer live in the source tree;
5. publication does not rebuild the binary.

### v5.3.0 provenance exception

`v5.3.0` is a documented exception to the normal publication path. During a GitHub Actions runner-queue delay, its package was built and published locally from release commit `9b0de6b424fb7e3bc0719c46f26fa8e01d902cc0`. The exact commit later passed [Native Release run 32989116200](https://github.com/Iviesever/msvc-quick-build/actions/runs/32989116200), but that run detected the existing Release and correctly skipped publication.

The published ZIP has SHA-256 `2bec70c09380cbd9b6749c5c504845a306e332ed29628590ebc5a214533affe3`; the later workflow artifact has SHA-256 `ec95ff280f6a5bf8a1f3d5fd3bf6cf66b2a0b676ae01de22baac3e9fb5d5da63`. Their seven-file manifests match, but the archives and `mqb.exe` binaries are not byte-identical. The `v5.3.0` tag and assets remain immutable; `v5.3.1` restores publication by the exact-commit `Native Release` workflow.

A release-workflow repair may also rerun the same version gate to recover from a failed, not-yet-published release. If that version's GitHub Release already exists, publication is skipped safely. If only the tag exists without a Release, the workflow fails closed instead of guessing or overwriting historical state.

Existing Releases/tags and binary assets remain immutable and are never overwritten by the workflow.

## 7. Development vs. release

Day-to-day development normally uses:

```powershell
.\tests\native\develop.ps1
```

Stable release adds pinned-seed bootstrap, Stage 1/Stage 2 closure, exact runtime-only package/checksum validation, and installer lifecycle gates.

See [`DEVELOPMENT_EN.md`](DEVELOPMENT_EN.md) for development workflow and [`ARCHITECTURE_EN.md`](ARCHITECTURE_EN.md) for the internal build model.
