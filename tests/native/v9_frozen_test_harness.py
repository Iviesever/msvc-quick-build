"""Test-only frozen helpers for consumed V9 protocols; never a product checkout.

The current production TU inventory must evolve. Historical protocols keep their
original whole-file pins and continue to REJECT the current inventory. Synthetic
controls use this authenticated 17-member snapshot instead. No pin, request,
historical artifact, runtime verifier or manual workflow is rewritten.
"""
from __future__ import annotations
from contextlib import contextmanager
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]
ORIGIN = "433c127566619009579f3f09df3e5627279b24da"
LAYOUT = "tests/native/assert_cpp_layout.ps1"
FIXTURE = "tests/native/fixtures/v9-replay/assert_cpp_layout.ps1.txt"
SYNTHETIC_COMMIT = "c" * 40
PINS = {
    ".github/workflows/v9-noop-rebound.yml": "a8f63d337682e56756e30ec40fbd4f8ed1d978b9c36ec68d21d6dc05c336a12e",
    ".github/workflows/v9-noop-samejob.yml": "2694f47771f2ca9397e7413a9095181ac374b0f57cb9e3c1234a233adc88f90b",
    ".github/workflows/v9-noop-validation.yml": "35f37126c30ce91320af2fa35c8f9046f7265551e63bde3d36188a2562feac53",
    "tests/native/acquire_seed.ps1": "eea47140fb535cc77dfaf9bbf70a738861a77475f36615dcc28f4282256116f1",
    "tests/native/assert_cpp_layout.ps1": "392a1a938d26cdf0f4320587519fee9be05f52978217774a8b23f7528f18ba68",
    "tests/native/build_mqb.ps1": "d6c90d2e9785f97fd0a176117c26841c3624699f5e7cfb7931d20e80a37e1826",
    "tests/native/check_external_noop_gate.py": "eec7f65d218aede93cda5030c9c10865eaf535075ba58b136f69dd02715b5bb7",
    "tests/native/collect_external_noop_boundary.ps1": "4b99ee4b1ad4de3608d3079175ff47bbd1de74e8f80dad715ac6b303698f9ebf",
    "tests/native/external_noop_boundary.py": "daad65d07b52edda2fa5a3f07c53c8c123b357a8bfe09039ffb151c43a45fee9",
    "tests/native/run_v9_noop_rebound.ps1": "a4a28f175f0fe02ce670e84a955e04a8394007fb4c373373ccc53f2505162141",
    "tests/native/run_v9_noop_samejob.ps1": "2ced1324191017920bd9df1366fafb79210e26498b8bd3df7f27a3bcaab8b9d2",
    "tests/native/run_v9_noop_validation.ps1": "00aae69c629ae6c1c08a9bd86766b68aea2c4b7db109e69637e551a7b46afa05",
    "tests/native/v9_noop_rebound.py": "a0fbc6b8cb8c1c5e98f069120e149f9c2610aeff60a194aa35cdb9b2c6dc5b35",
    "tests/native/v9_noop_rebound_preparation.json": "5d3d24888dc1d4ab54b81eba02e851d291ac15e74f330f3b2909c61eb43ac6f2",
    "tests/native/v9_noop_samejob.py": "a344082c3887e11069fb4b1352ae4b1e4366aa95e689c65205571a3e3b8cd366",
    "tests/native/v9_noop_validation.py": "d1621c5fe9a00ea32d23a494a97b393c7b40e98594cab7a7ef3756da84c395b6",
    "tests/native/v9_noop_validation_failed_measurement.zip": "c0ac5b61a9e3e0ca7956794cf221a1dd4c2bb13a10ceeb916f58f01f14a9c30b"
}

def canonical(name: str, raw: bytes) -> bytes:
    return raw if name.endswith(".zip") else raw.replace(b"\r\n", b"\n")

def verified_members(current: Path) -> dict[str, bytes]:
    result = {}
    for name, expected in PINS.items():
        source = current / (FIXTURE if name == LAYOUT else name)
        if not source.is_file() or source.is_symlink():
            raise ValueError("Missing/unsafe frozen test member: " + name)
        data = canonical(name, source.read_bytes())
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("Frozen test member changed: " + name)
        result[name] = data
    return result

def build_snapshot(current: Path, output: Path) -> dict:
    # Authenticate everything before creating a destination. No current source edit.
    members = verified_members(current)
    if output.exists():
        raise FileExistsError("Fresh test snapshot required")
    output.mkdir(parents=True)
    for name, data in members.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(data)
    return dict(kind="synthetic_test_helper_snapshot", origin=ORIGIN,
                files=len(members), member_sha256=dict(PINS),
                full_product_checkout=False, execution_authorized=False)

@contextmanager
def pinned_test_harness(current: Path = ROOT):
    with tempfile.TemporaryDirectory(prefix="v9-frozen-test-") as temp:
        output = Path(temp) / "helpers"
        build_snapshot(current, output)
        yield output

def synthetic_archive(snapshot: Path, destination: Path) -> None:
    # This is intentionally NOT an archive of the current or historical Git commit.
    # The real current source.zip remains separately archived by contract CI.
    with ZipFile(destination, "x") as archive:
        archive.comment = SYNTHETIC_COMMIT.encode("ascii")
        for name, expected in PINS.items():
            data = (snapshot / name).read_bytes()
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError("Snapshot changed before synthetic archive: " + name)
            archive.writestr(name, data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    args = parser.parse_args()
    metadata = build_snapshot(ROOT, args.output)
    synthetic_archive(args.output, args.archive)
    print(json.dumps(metadata, sort_keys=True))
