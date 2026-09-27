"""Static budget and clock invariants; no benchmarks or GitHub dispatch."""
from pathlib import Path
import hashlib
import re
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
LAUNCH_PINS = {'Invoke-TimedMqb': 'b85002d8a5d216ce651f997f45c2a60927551467b00e970834b175a10fc5a39b', 'Invoke-UntimedMqb': '603327371c8e70f0a1d98abc0091d1c1e8c767ae010fe067221c610c9274b38e'}
GATE_SHA = 'eec7f65d218aede93cda5030c9c10865eaf535075ba58b136f69dd02715b5bb7'
SCHEDULE = [
    'cold','no-op','single-tu','public-header','build-run','link-only',
    'target-scale-cold','target-scale-no-op','target-scale-no-op-auto',
    'target-scale-no-op-j1','target-scale-single-tu','target-scale-common-header-prime',
    'target-scale-common-header-no-op','discovery-cold','discovery-no-op','discovery-header',
    'timings-prime','timings-enabled-no-op','timings-disabled-no-op','modules-cold','modules-no-op',
]

def source(name): return (HERE/name).read_text(encoding='utf-8')

def gate_digest(raw):
    # Git checkout may use CRLF. Only compare its LF representation; retain all
    # other bytes, including a lone CR, BOM, content edits and trailing whitespace.
    return hashlib.sha256(raw.replace(b"\r\n", b"\n")).hexdigest()

def launcher(text, name, end):
    part = text.split('function '+name+' {',1)[1].split('function '+end+' {',1)[0]
    return part[part.index('    if ($InvocationEvidenceDirectory) {'):part.index('    } catch { $captureFailure')]

def preservation(text):
    required = ["$arguments.RawEvidenceDirectory =", "'failure.json'", "'INVALID'",
                'if (-not $EvidenceDirectory)', 'pair-$pairIndex.completed.json', 'throw']
    if any(x not in text for x in required): raise ValueError('preservation wiring changed')
    if 'continue-on-error' in text or 'exit 0' in text: raise ValueError('failure swallowed')

class EvidenceContracts(unittest.TestCase):
    def test_legacy_launch_and_clock_blocks_are_byte_identical(self):
        s=source('benchmark_mqb.ps1')
        for name,end in [('Invoke-TimedMqb','Invoke-UntimedMqb'),('Invoke-UntimedMqb','Assert-CompileCacheCounts')]:
            self.assertEqual(hashlib.sha256(launcher(s,name,end).encode()).hexdigest(),LAUNCH_PINS[name])

    def test_raw_evidence_never_selects_observed_clock(self):
        s=source('benchmark_mqb.ps1')
        self.assertIn("if ($InvocationEvidenceDirectory) { throw 'Raw and observed",s)
        c=source('compare_mqb_benchmarks.ps1')
        self.assertNotIn('-InvocationEvidenceDirectory',c)
        self.assertIn('$arguments.RawEvidenceDirectory',c)

    def test_all_21_calls_and_two_unreported_primes_retained(self):
        s=source('benchmark_mqb.ps1')
        self.assertEqual(re.findall(r"Invoke-(?:Timed|Untimed)Mqb -Scenario '([^']+)'",s),SCHEDULE)
        self.assertEqual(len(re.findall(r"Invoke-TimedMqb -Scenario '",s)),18)
        self.assertEqual(len(re.findall(r"Invoke-UntimedMqb -Scenario '",s)),3)

    def test_instrumentation_has_eight_calls_not_eight_samples(self):
        s=source('verify_performance_instrumentation.ps1')
        self.assertEqual(len(re.findall(r'= Invoke-MqbCapture\b',s)),8)
        self.assertEqual(len(re.findall(r'& \$MqbPath @Arguments',s)),1)

    def test_root_mqb_budget_181_is_only_a_static_ceiling(self):
        seed=source('acquire_seed.ps1');build=source('build_mqb.ps1')
        self.assertEqual(seed.count('& $seedMqb --help'),1)
        self.assertEqual(build.count('& $BuilderMqbPath @arguments'),1)
        self.assertEqual(build.count('& $built --help'),1)
        self.assertEqual(1+2*2+8+4*2*len(SCHEDULE),181)

    def test_original_gate_is_not_modified_to_admit_observed_samples(self):
        self.assertEqual(gate_digest((HERE/'check_external_noop_gate.py').read_bytes()),GATE_SHA)

    def test_gate_digest_accepts_only_checkout_newline_difference(self):
        raw=(HERE/'check_external_noop_gate.py').read_bytes().replace(b"\r\n", b"\n")
        self.assertEqual(gate_digest(raw),GATE_SHA)
        self.assertEqual(gate_digest(raw.replace(b"\n",b"\r\n")),GATE_SHA)
        for changed in (raw+b' ', b'\xef\xbb\xbf'+raw, raw.replace(b'\n',b'\r',1),
                        raw.replace(b'> 1',b'> 2',1) if b'> 1' in raw else raw+b'# changed'):
            self.assertNotEqual(gate_digest(changed),GATE_SHA)

    def test_observed_function_fixture_initializes_disabled_raw_mode(self):
        s=source('verify_benchmark_attribution.ps1')
        self.assertIn('$RawEvidenceDirectory = $null',s)
        self.assertLess(s.index('$RawEvidenceDirectory = $null'),
                        s.index("$result = Invoke-ObservedMqb"))
        self.assertIn("'malformed-timing-retained-before-parse-failure'",s)
        self.assertIn("'inactive-observer-retains-legacy-clock-and-unavailable-counters'",s)

    def test_historical_attribution_research_remains_disabled(self):
        s=(ROOT/'.github/workflows/benchmark-attribution.yml').read_text()
        contract,investigation=s.split('  investigate:\n',1)
        self.assertIn('verify_benchmark_attribution.ps1',contract)
        self.assertIn('    if: ${{ false }}',investigation)
        self.assertLess(investigation.index('    if: ${{ false }}'),investigation.index('    steps:'))
        self.assertIn('d01bb920e030202c33639f59139491ac7f49117c',investigation)
        self.assertIn('283b50733601926840fdcd0474d10a06a4f75c7d',investigation)

    def test_record_write_precedes_validation(self):
        s=source('benchmark_mqb.ps1')
        for name,end in [('Invoke-TimedMqb','Invoke-UntimedMqb'),('Invoke-UntimedMqb','Assert-CompileCacheCounts')]:
            part=s.split('function '+name+' {',1)[1].split('function '+end+' {',1)[0]
            self.assertLess(part.index("'.started.json'"),part.index('    if ($InvocationEvidenceDirectory)'))
            self.assertLess(part.index("'.result.json'"),part.index('if ($exitCode -ne 0)'))

    def test_instrumentation_record_precedes_timing_contract(self):
        s=source('verify_performance_instrumentation.ps1')
        self.assertLess(s.index("'.result.json'"),s.index('if ($exitCode -ne 0)'))
        self.assertLess(s.index("'.started.json'"),s.index('Push-Location $WorkingDirectory'))

    def test_partial_comparison_is_never_a_success_report(self):
        c=source('compare_mqb_benchmarks.ps1');preservation(c)
        self.assertIn("kind = 'partial-diagnostic-only'",c)
        self.assertIn('NOT_a_comparison_report = $true',c)
        self.assertIn("throw 'Refusing to replace an existing comparison report.'",c)

    def test_bypass_mutations_are_rejected(self):
        s=source('compare_mqb_benchmarks.ps1')
        for text in [s.replace('if (-not $EvidenceDirectory)','if ($true)'),s.replace("'failure.json'","'discarded.json'"),s+'\nexit 0']:
            with self.assertRaises(ValueError):preservation(text)

    def test_writer_is_create_new_without_launch_or_clock(self):
        s=source('performance_evidence_files.ps1')
        self.assertIn('[IO.FileMode]::CreateNew',s)
        for token in ('Start-Process','Stopwatch','Remove-Item','GetEnvironmentVariables','retry','& $'):
            self.assertNotIn(token,s.split('\n',1)[1])

    def test_workflow_keeps_four_pairs_and_raw_upload(self):
        s=(ROOT/'.github/workflows/performance-evidence.yml').read_text()
        self.assertEqual(s.count('-Iterations 4'),1)
        self.assertIn('            performance-out/evidence/',s)
        self.assertIn("-EvidenceDirectory (Join-Path $env:GITHUB_WORKSPACE 'performance-out/evidence/comparison')",s)
        self.assertIn("-EvidenceDirectory (Join-Path $env:GITHUB_WORKSPACE 'performance-out/evidence/instrumentation')",s)
        self.assertLess(s.index('Preserve request and sources before preparation'),s.index('Acquire one pinned historical'))
        self.assertEqual(s.count('Start-Transcript -LiteralPath'),3)
        self.assertEqual(s.count('-NoClobber'),3)
        self.assertEqual(s.count('finally { Stop-Transcript | Out-Null }'),3)

    def test_fault_workflow_does_not_enable_real_measurement(self):
        s=(ROOT/'.github/workflows/performance-evidence-contracts.yml').read_text()
        self.assertIn('pull_request:',s)
        for token in ('workflow_dispatch:','perf:', 'acquire_seed.ps1', 'build_mqb.ps1'):
            self.assertNotIn(token,s)
        self.assertIn('test_performance_evidence_control.ps1',s)

if __name__=='__main__':unittest.main(verbosity=2)
