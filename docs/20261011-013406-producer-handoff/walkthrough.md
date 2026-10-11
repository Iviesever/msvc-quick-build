# Verification walkthrough: closed diagnostic 010

Status: `010_CLOSED_DOCS_ONLY_HANDOFF_011_PROPOSED`.

This walkthrough indexes the work completed under the continuation instruction.
The retained branch is `feat/producer-clean-handoff-20261011`, in its existing
isolated linked worktree. The integration choice is to retain that branch for
continued investigation; main and the two existing draft PRs remain unchanged.

## What changed and why

[009](native_startup_result_009.md) ended with actual C exit 0xC0000142 but no
loader strings naming an initializer. The
[010 plan](native_startup_plan_010.md) made one fresh child EXE copy and changed
only the existing PE GlobalFlagsSet DWORD from 0 to 2. New PE/image helpers,
a 009-derived diagnostic translation unit, a bounded runner and meaningful
pure-Python tests implemented and checked that single attempt. Existing token,
object DACL, IPC and owned cleanup behavior remained frozen.

The execution commit is `083d581af6822c04754799e2db92636f0c891fd8`. It added
six native-source/helper/test files and the exact 010 plan, and changed only
the selecting workflow, .gitattributes and the two progress documents.
All final source identities are in the immutable original artifact.

Source review corrected sequence inversion, missing-hFile completeness,
final-pin deadline coverage and PE-field directory aliasing before activation.
The final tests below and independent source reviews preceded registration.
No rejected source candidate was compiled or executed natively.

## Actual final source-test output

Command in the retained worktree:

```text
python3 -B -m unittest discover -s tests/native -p 'test_producer_loader_snaps_*.py' -v
```

The following is the complete actual output preserved in
`source-static-checks-010.json` (SHA-256
`b64ff44ec59fb56f89f9f0ce597d2a2ecdaaaa1ec42b75966dcd123853304d64`).
Exit code was 0. This is the original final source check, not a claimed rerun
during this documentation closeout.

```text
test_booleans_do_not_substitute_for_numeric_counts (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_booleans_do_not_substitute_for_numeric_counts) ... ok
test_budget_truncation_cannot_claim_complete (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_budget_truncation_cannot_claim_complete) ... ok
test_complete_diagnostic_remains_inconclusive (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_complete_diagnostic_remains_inconclusive) ... ok
test_complete_requires_all_three_physical_bindings (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_complete_requires_all_three_physical_bindings) ... ok
test_duplicate_json_key_is_not_silently_replaced (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_duplicate_json_key_is_not_silently_replaced) ... ok
test_each_registered_counter_is_enforced (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_each_registered_counter_is_enforced) ... ok
test_incomplete_capture_keeps_false_binding_without_qualification (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_incomplete_capture_keeps_false_binding_without_qualification) ... ok
test_success_exit_or_product_authority_is_refused (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_success_exit_or_product_authority_is_refused) ... ok
test_wait_counts_and_byte_counts_must_reconcile (test_producer_loader_snaps_capture.LoaderCaptureAdmissionTests.test_wait_counts_and_byte_counts_must_reconcile) ... ok
test_alignment_header_and_image_bounds_are_checked (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_alignment_header_and_image_bounds_are_checked) ... ok
test_checksum_is_not_silently_recalculated (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_checksum_is_not_silently_recalculated) ... ok
test_config_structure_size_must_cover_flags_and_fit_directory (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_config_structure_size_must_cover_flags_and_fit_directory) ... ok
test_declared_optional_header_and_directory_ranges_are_checked (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_declared_optional_header_and_directory_ranges_are_checked) ... ok
test_directory_cannot_use_raw_padding_outside_virtual_size (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_directory_cannot_use_raw_padding_outside_virtual_size) ... ok
test_directory_cannot_use_zero_filled_virtual_tail (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_directory_cannot_use_zero_filled_virtual_tail) ... ok
test_duplicate_rdata_name_is_ambiguous_even_without_overlap (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_duplicate_rdata_name_is_ambiguous_even_without_overlap) ... ok
test_each_declared_nonempty_directory_needs_a_full_mapping (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_each_declared_nonempty_directory_needs_a_full_mapping) ... ok
test_flag_baselines_are_explicit_and_cannot_be_merged (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_flag_baselines_are_explicit_and_cannot_be_merged) ... ok
test_hard_image_limit_has_no_implicit_expansion (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_hard_image_limit_has_no_implicit_expansion) ... ok
test_input_and_bound_types_are_strict (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_input_and_bound_types_are_strict) ... ok
test_load_config_requires_read_only_initialized_rdata (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_load_config_requires_read_only_initialized_rdata) ... ok
test_missing_or_partially_present_load_config_is_rejected (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_missing_or_partially_present_load_config_is_rejected) ... ok
test_pair_refuses_adjacent_and_distant_unrelated_mutation (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_pair_refuses_adjacent_and_distant_unrelated_mutation) ... ok
test_pair_refuses_size_change_and_wrong_patch (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_pair_refuses_size_change_and_wrong_patch) ... ok
test_patch_field_cannot_alias_another_declared_directory (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_patch_field_cannot_alias_another_declared_directory) ... ok
test_pe_offset_cannot_point_inside_dos_or_past_file (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_pe_offset_cannot_point_inside_dos_or_past_file) ... ok
test_raw_overlap_is_rejected_before_rva_mapping (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_raw_overlap_is_rejected_before_rva_mapping) ... ok
test_raw_ranges_must_be_file_backed_and_outside_headers (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_raw_ranges_must_be_file_backed_and_outside_headers) ... ok
test_relocated_headers_and_sections_do_not_reuse_previous_offset (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_relocated_headers_and_sections_do_not_reuse_previous_offset) ... ok
test_reserved_directories_and_embedded_certificates_are_refused (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_reserved_directories_and_embedded_certificates_are_refused) ... ok
test_section_count_and_symbol_table_are_bounded (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_section_count_and_symbol_table_are_bounded) ... ok
test_short_dos_or_wrong_magic_is_rejected (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_short_dos_or_wrong_magic_is_rejected) ... ok
test_signature_architecture_and_executable_profile_are_checked (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_signature_architecture_and_executable_profile_are_checked) ... ok
test_truncated_section_table_is_not_accepted_as_zero_sections (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_truncated_section_table_is_not_accepted_as_zero_sections) ... ok
test_valid_layout_yields_derived_offset_and_full_fingerprints (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_valid_layout_yields_derived_offset_and_full_fingerprints) ... ok
test_valid_single_dword_pair_reparses_and_preserves_all_other_bytes (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_valid_single_dword_pair_reparses_and_preserves_all_other_bytes) ... ok
test_virtual_overlap_and_rva_overflow_are_rejected (test_producer_loader_snaps_pe.LoaderSnapsPeTests.test_virtual_overlap_and_rva_overflow_are_rejected) ... ok

----------------------------------------------------------------------
Ran 37 tests in 0.004s

OK
```

The suite covers 28 PE and 9 capture-admission cases. It does not execute a
Windows native image or the product workload. Earlier red/green iterations
remain in the evidence package as development records, not additional runs.

## One Windows execution and its actual result

[Registration 6107842676](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6107842676)
was read back before the one expected-head branch update.
[Run 38130540974](https://github.com/Iviesever/msvc-quick-build/actions/runs/38130540974)
is workflow 380958963, number 10 / attempt 1. One compiler driver passed
/W4 /WX with exit 0 and compiler Job total 2. One native invocation produced
the original 45-member artifact, which uploaded successfully.

The workflow completed failure. The actual native outer Job total is 3 against
the fixed required 2, so result.json remains
`INCONCLUSIVE / NATIVE_EXECUTION_INCOMPLETE` without a native_summary.
This known failed admission is preserved; it is not a passed integration test.

Separate raw reviews fully checked 4,472 records, 646 events/continuations,
628 loader-string prefixes, the exact PE-byte change and actual image bindings.
The loader explicitly reports bcrypt.dll DLL_PROCESS_ATTACH failure.
Internal API/access cause is unknown, startup and IPC are absent, and the
additional counted process has no established identity. See the
[full 010 result](native_startup_result_010.md) for sequences, hashes, scope and
all qualification limits.

Root and two independent reviewers read and reconciled the complete artifact.
[Closure 6108033087](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6108033087)
was then written and fully read back.

## Retained handoff and closeout scope

The [011 handoff](native_startup_handoff_011.md) proposes exact actual bcrypt
DLL identity/bytes followed by bounded offline analysis. The candidate 8 MiB
hex transport exceeds the existing conservative byte and row envelopes.
Its combined budget is the first unresolved task. There is no 011 implementation,
registration, activation or native run.

This closeout updates issue.md and implementation_plan.md and adds the 010
result, 011 handoff and this walkthrough. It preserves .gitattributes, the
workflow, the registered 010 plan and all executable sources. The original
010 proposal/plan status lines are historical snapshots superseded by the
result and closure, rather than rewritten registered inputs. Documentation
links and the exact five-path diff receive a separate review before commit.

Main remains `87f86770cfc7e1941603074ace914e0c7faf10d3`; PR250 and PR248 remain
open drafts/unmerged, with PR250 HOLD. Product cpp/VERSION, consumer/cost DEFER,
the 21,000-byte cost preparation, disabled M1b/service jobs and all earlier
evidence remain intact. There is no merge, cleanup, deletion or ordinary MQB.
A1-A5 and deletion authority fields remain false.

Cumulative after 010: ten prerequisite workflow runs, nine compiler drivers,
compiler Job TotalProcesses sum 21, six native fixtures, zero ordinary MQB.
The branch/worktree is retained so the concrete 011 budget and collector task
can be resumed without re-running or reclassifying 010.
