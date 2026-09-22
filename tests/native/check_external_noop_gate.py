#!/usr/bin/env python3
"""Read-only enforcement of the registered external no-op acceptance rule.

Exit 0: threshold not crossed (NOT overall acceptance); 1: HOLD; 2: INVALID.
The collector, full evidence/provenance audit and other scenario review remain
separate. This tool never runs MQB, removes samples or edits its input.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, InvalidOperation, localcontext
from fractions import Fraction
import hashlib
import io
import json
from pathlib import Path
import statistics
import sys
from zipfile import BadZipFile, ZipFile

SCENARIO = 'timings-disabled-no-op'
SCENARIOS = (
    'cold', 'no-op', 'single-tu', 'public-header', 'build-run', 'link-only',
    'target-scale-cold', 'target-scale-no-op', 'target-scale-no-op-auto',
    'target-scale-no-op-j1', 'target-scale-common-header-no-op',
    'target-scale-single-tu', 'discovery-cold', 'discovery-no-op',
    'discovery-header', 'modules-cold', 'modules-no-op',
    'timings-enabled-no-op', SCENARIO,
)
ORDER = ['baseline-candidate', 'candidate-baseline'] * 2
REPORT_LIMIT = 32 * 1024 * 1024
ARCHIVE_LIMIT = 128 * 1024 * 1024


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def number(value: object) -> Fraction:
    require(type(value) in (int, float, Decimal), 'timing must be a JSON number')
    try:
        d = Decimal(str(value))
    except InvalidOperation as error:
        raise ValueError('invalid timing') from error
    require(d.is_finite(), 'timing must be finite')
    # Bound arithmetic resources; do not round values before the strict rule.
    require(len(d.as_tuple().digits) <= 100 and abs(d.as_tuple().exponent) <= 100,
            'timing numeric representation exceeds resource limit')
    return Fraction(d)


def display(value: Fraction) -> str:
    with localcontext() as ctx:
        ctx.prec = 40
        return str(Decimal(value.numerator) / Decimal(value.denominator))


def evaluate_pairs(pairs: object) -> dict:
    require(isinstance(pairs, list) and len(pairs) == 4, 'exactly four external pairs required')
    seen = set()
    values = []
    for row in pairs:
        require(isinstance(row, dict), 'pair must be an object')
        i = row.get('pair')
        require(type(i) is int and 1 <= i <= 4 and i not in seen, 'invalid/duplicate pair ID')
        seen.add(i)
        require(row.get('scenario') == SCENARIO, 'wrong external scenario')
        require(row.get('orientation') == ORDER[i - 1], 'pair orientation mismatch')
        a, b = number(row.get('baseline_total_ms')), number(row.get('candidate_total_ms'))
        require(a > 0 and b >= 0, 'baseline must be positive; candidate nonnegative')
        for side, total in (('baseline', a), ('candidate', b)):
            sample = row.get(side)
            require(isinstance(sample, dict), 'missing raw external sample')
            require(sample.get('scenario') == SCENARIO, 'raw sample scenario mismatch')
            require(type(sample.get('iteration')) is int and sample['iteration'] == 1,
                    'raw sample must be one fixture iteration')
            require(sample.get('measurement_source') == 'external_stopwatch',
                    'not an uninstrumented external stopwatch sample')
            require(type(sample.get('timing_schema_version')) is int and
                    sample['timing_schema_version'] == 0, 'unexpected external timing schema')
            require(number(sample.get('total_ms')) == total, 'raw/top-level timing mismatch')
            for key in ('attribution', 'counters', 'counter_breakdown'):
                require(key in sample and sample[key] is None, 'external vector must remain null: ' + key)
            for key in ('compile_hits', 'compile_misses', 'link_hits', 'link_misses',
                        'archive_hits', 'archive_misses'):
                require(type(sample.get(key)) is int and sample[key] == -1,
                        'external uncollected count must remain -1: ' + key)
        values.append((i, a, b, b - a, (b - a) * 100 / a))
    values.sort()
    delta = statistics.median(v[3] for v in values)
    percent = statistics.median(v[4] for v in values)
    crossed = delta > 1 and percent > 10
    return {
        'schema_version': 1, 'scenario': SCENARIO, 'pair_count': 4,
        'pairs': [dict(pair=i, orientation=ORDER[i-1], baseline_ms=display(a),
                       candidate_ms=display(b), delta_ms=display(d), delta_pct=display(p))
                  for i, a, b, d, p in values],
        'median_delta_ms': display(delta), 'median_delta_pct': display(percent),
        'threshold_ms': '1', 'threshold_pct': '10', 'strict_comparison': True,
        'combination': 'AND', 'crossed': crossed,
        'decision': 'HOLD' if crossed else 'threshold_not_crossed',
        'exact_rational_comparison': True, 'cause_established': False,
        'overall_acceptance_proven': False,
    }


def evaluate_report(report: object) -> dict:
    require(isinstance(report, dict), 'comparison report must be an object')
    require(type(report.get('schema_version')) is int and report['schema_version'] == 3,
            'registered comparison schema 3 required')
    require(type(report.get('pair_count')) is int and report['pair_count'] == 4,
            'registered four-pair budget required')
    require(report.get('execution_order') == ORDER, 'execution order mismatch')
    require(report.get('pairing') == 'alternating baseline-candidate / candidate-baseline',
            'pairing policy mismatch')
    require(report.get('required_scenarios') == list(SCENARIOS), 'registered 19-scenario manifest mismatch')
    rows = report.get('paired_samples')
    require(isinstance(rows, list) and len(rows) == 76, 'incomplete 19 by 4 paired sample grid')
    seen = set()
    for row in rows:
        require(isinstance(row, dict), 'sample row must be an object')
        name, i = row.get('scenario'), row.get('pair')
        require(type(name) is str and name in SCENARIOS and type(i) is int and 1 <= i <= 4,
                'invalid sample grid identity')
        require((name, i) not in seen, 'duplicate sample grid row')
        seen.add((name, i))
        require(row.get('orientation') == ORDER[i - 1], 'sample grid orientation mismatch')
    result = evaluate_pairs([r for r in rows if r['scenario'] == SCENARIO])
    result['grid_rows_checked'] = len(seen)
    # Rounded comparison/delta summaries are deliberately not decision inputs.
    result['scope'] = 'external no-op threshold and grid structure; not full provenance/scenario audit'
    return result


def strict_json(data: bytes) -> object:
    def members(items):
        result = {}
        for key, value in items:
            require(key not in result, 'duplicate JSON member: ' + key)
            result[key] = value
        return result
    def reject_constant(value):
        raise ValueError('nonfinite JSON constant: ' + value)
    return json.loads(data.decode('utf-8-sig'), parse_float=Decimal,
                      parse_constant=reject_constant, object_pairs_hook=members)


def read_bounded(path: Path, limit: int) -> bytes:
    with path.open('rb') as f:
        data = f.read(limit + 1)
    require(len(data) <= limit, 'input size limit exceeded')
    return data


def load_report(path: Path, artifact_sha256: str | None = None) -> tuple[object, dict]:
    data = read_bounded(path, ARCHIVE_LIMIT if artifact_sha256 is not None else REPORT_LIMIT)
    digest = hashlib.sha256(data).hexdigest()
    provenance = {'input_sha256': digest, 'input_bytes': len(data), 'archived_tools_executed': False}
    if artifact_sha256 is not None:
        require(len(artifact_sha256) == 64 and all(c in '0123456789abcdef' for c in artifact_sha256),
                'expected lowercase artifact SHA256 required')
        require(digest == artifact_sha256, 'artifact SHA256 mismatch')
        with ZipFile(io.BytesIO(data)) as archive:
            names = archive.namelist()
            require(len(names) == len(set(names)), 'duplicate archive member')
            matches = [n for n in names if n.rsplit('/', 1)[-1] == 'benchmark-comparison.json']
            require(len(matches) == 1, 'unique archived comparison report required')
            info = archive.getinfo(matches[0])
            require(info.file_size <= REPORT_LIMIT and not info.is_dir(), 'archived report size limit')
            with archive.open(info) as member:
                data = member.read(REPORT_LIMIT + 1)
            require(len(data) <= REPORT_LIMIT, 'expanded report size limit')
            provenance['report_member'] = info.filename
        provenance['artifact_sha256'] = digest
    provenance['report_sha256'] = hashlib.sha256(data).hexdigest()
    return strict_json(data), provenance


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='comparison JSON, or ZIP with --artifact-sha256')
    parser.add_argument('--artifact-sha256', help='pins archive input; does not execute its binaries')
    parser.add_argument('--output', type=Path, required=True, help='new decision JSON (never overwrite)')
    args = parser.parse_args(argv)
    try:
        report, origin = load_report(args.input, args.artifact_sha256)
        result = evaluate_report(report)
        result.update(origin)
        code = 1 if result['crossed'] else 0
    except (ValueError, TypeError, OSError, BadZipFile, EOFError, RecursionError,
            NotImplementedError, RuntimeError) as error:
        result = {'schema_version': 1, 'decision': 'INVALID', 'error': str(error),
                  'overall_acceptance_proven': False, 'archived_tools_executed': False}
        code = 2
    result['exit_code'] = code
    try:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('x', encoding='utf-8', newline='\n') as f:
            json.dump(result, f, ensure_ascii=False, indent=2, allow_nan=False)
            f.write('\n')
    except OSError as error:
        print('INVALID: cannot create a fresh decision file: ' + str(error), file=sys.stderr)
        return 2
    print(result['decision'])
    return code


if __name__ == '__main__':
    raise SystemExit(main())
