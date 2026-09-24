"""Extract the pinned production V9 grammar, not a hand-written reference parser.
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

def render(repo: Path) -> tuple[str, dict]:
    source = {}
    for name, expected in PINS.items():
        raw = canonical((repo / name).read_bytes())
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError('Pinned production source changed: ' + name)
        source[name] = raw.decode('utf-8')
    cache, primitives, _ = source.values()
    pieces = [
        section(cache, 'constexpr std::string_view cache_magic', 'struct ToolPaths'),
        section(cache, '[[nodiscard]] fs::path stable_path(', '[[nodiscard]] bool same_path('),
        section(cache, '[[nodiscard]] bool environment_name_equal(', '[[nodiscard]] const EnvironmentVariable* find_environment('),
        section(cache, '[[nodiscard]] bool write_quoted(', '[[nodiscard]] std::optional<MsvcToolchain> try_reuse_visual_studio_cache('),
    ]
    conversion = section(primitives, 'std::string path_to_utf8(', 'std::string trim_ascii(')
    header = '''// GENERATED FROM PINNED PRODUCTION TEXT; DO NOT EDIT.
#pragma once
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "mqb/process/Process.hpp"
namespace mqb_v9_oracle {
namespace fs = std::filesystem;
using mqb::process::EnvironmentVariable;
namespace detail {
''' + conversion + '\n}\n' + '\n'.join(pieces) + '\n}\n'
    metadata = dict(schema=1, production_source_hashes=PINS,
        generated_header_sha256=hashlib.sha256(header.encode()).hexdigest(),
        extraction='exact fixed text; grammar/record/path conversion/name predicate unchanged',
        excluded='filesystem freshness, toolchain adoption and side effects; no production integration',
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
