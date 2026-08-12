# MQB Self-Hosting and Release Contract

**[简体中文](SELF_HOSTING.md) | English**

This document defines the **bootstrap, validation, and publication boundary** for stable releases. Day-to-day development lives in [`DEVELOPMENT_EN.md`](DEVELOPMENT_EN.md); CLI, source-layout, and configuration details are intentionally not repeated here.

## 1. Why a seed is required

MQB builds itself with MQB, so the first generation of the current source must be started by an already-existing trusted `mqb.exe`.

Stable v5 pins the historical `v5.0.0-rc.2` release binary as the bootstrap seed. CI validates:

- the fixed Release ZIP SHA-256;
- the executable's expected MQB version identity.

The seed is used only to build **Stage 0** from the current source. It never enters the stable package.

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

Stage 0, Stage 1, and Stage 2 all come from the **same candidate commit's current source**, and every generation is built by MQB.

Meaning:

- **Stage 0** proves that the historical seed can build the current implementation;
- **Stage 1** is built again by the current implementation and becomes the release candidate;
- **Stage 2** is built by Stage 1 after clearing MQB build state, proving that clean self-host closure still holds.

## 3. Project description and version source

MQB builds itself from:

```text
cpp/mqb.json
```

That manifest must match the real production source set, but the number of production translation units is not a stable contract.

The repository's single release-version source is:

```text
release/VERSION
```

The build driver injects that value as a structured `MQB_VERSION` definition. `cpp/mqb.json` does not duplicate the release version.

## 4. Release-blocking validation

A stable candidate must prove, on the same candidate commit, that:

1. the pinned seed checksum and executable identity are correct;
2. the seed can build the current Stage 0;
3. Stage 0 passes the full Release native test suite;
4. Stage 0 can build Stage 1;
5. after clearing MQB build state, Stage 1 can build Stage 2;
6. Stage 1 and Stage 2 report the correct release version;
7. the `mqb.exe` inside the stable ZIP is byte-identical to the already-validated Stage 1 binary;
8. the exact package manifest and SHA-256 sidecar are correct;
9. the packaged installer passes install / reinstall / uninstall lifecycle validation.

Any failure blocks stable publication.

## 5. Stable package rules

The stable ZIP:

- contains the already-validated Stage 1 `mqb.exe`;
- does not contain the historical seed;
- does not contain Stage 0;
- does not rebuild the binary during publication.

For version `X.Y.Z`:

```text
msvc-quick-build-vX.Y.Z-windows-x64.zip
msvc-quick-build-vX.Y.Z-windows-x64.zip.sha256
```

See [`INSTALLATION_EN.md`](INSTALLATION_EN.md) for installation behavior.

## 6. Tag publication

Stable publication uses an immutable-artifact model:

- the pushed `vX.Y.Z` tag must exactly match `release/VERSION`;
- the publication job consumes the ZIP and checksum that were **already validated by the same workflow run**;
- publication does not rebuild, avoiding a situation where one artifact is validated and a different artifact is released.

Historical releases and tags are not rewritten when later documentation or implementation changes.

## 7. Development vs. stable release

Day-to-day development only needs to validate the current MQB and native test suite, normally through:

```powershell
.\tests\native\develop.ps1
```

Stable release adds the pinned-seed bootstrap, Stage 1/Stage 2 closure, exact package/checksum validation, and installer lifecycle gates.

See [`DEVELOPMENT_EN.md`](DEVELOPMENT_EN.md) for development workflow and [`ARCHITECTURE_EN.md`](ARCHITECTURE_EN.md) for the internal build model.
