"""Materialize the frozen original V9 grammar, never the changing product parser.
Only generates test input/header metadata; never executes MQB or a performance run.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

PINS = {
    'cpp/src/msvc/toolchain/VisualStudioToolchainCache.cpp': 'bcc7ca2d61f9365b06e558ff17c096059f7a2c257d8bcf08a37c5250e94a40d3',
    'cpp/src/msvc/toolchain/ToolchainDiscoveryPrimitives.cpp': 'a7a0833bfaaba19c2e4f0617e58cb91b7d90f517bb1e0c52ffe65e0031f4c6eb',
    'cpp/include/mqb/process/Process.hpp': 'd0b3e225bcdeeda6a89cd97f1cf1b2e218d9b38f81c2041a2c9c3872ccf1c09e',
}

def canonical(raw: bytes) -> bytes:
    return raw.replace(b'\r\n', b'\n')

def section(text: str, first: str, following: str) -> str:
    if text.count(first) != 1 or text.count(following) != 1:
        raise ValueError('Non-unique exact extraction anchors')
    lo, hi = text.index(first), text.index(following)
    if hi <= lo:
        raise ValueError('Reversed extraction anchors')
    return text[lo:hi]

REFERENCE_COMMIT = 'ed76d13df14acd680d5b523a27aa52fc99a1b09c'
REFERENCE_PATH = 'tests/native/v9_reader_baseline.hpp'
REFERENCE_SHA = 'da5b64cd2e2e4d3d6b2cdb592a2c6e71bcb98da9eef40bb98c3c2a378b7dcfed'

def render(repo: Path) -> tuple[str, dict]:
    # Immutable output of the original exact-section generator at REFERENCE_COMMIT.
    # Never refresh this to accept a new production parser as its own oracle.
    raw = canonical((repo / REFERENCE_PATH).read_bytes())
    if hashlib.sha256(raw).hexdigest() != REFERENCE_SHA:
        raise ValueError('Frozen V9 reference changed')
    dependency = 'cpp/include/mqb/process/Process.hpp'
    if hashlib.sha256(canonical((repo / dependency).read_bytes())).hexdigest() != PINS[dependency]:
        raise ValueError('Frozen reference record dependency changed')
    header = raw.decode('utf-8')
    metadata = dict(schema=1, production_source_hashes=PINS,
        reference_commit=REFERENCE_COMMIT, reference_path=REFERENCE_PATH,
        generated_header_sha256=REFERENCE_SHA,
        extraction='frozen exact original generator output; not current production grammar',
        excluded='filesystem freshness, toolchain adoption and side effects; no equivalence beyond tested domain',
        new_study_calls=0, new_etw_sessions=0, performance_verified=False, clears_hold=False)
    return header, metadata

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    header, metadata = render(args.repo)
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output/'v9_oracle.hpp').write_text(header, encoding='utf-8', newline='\n')
    (args.output/'oracle-manifest.json').write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf-8')

if __name__ == '__main__':
    main()
