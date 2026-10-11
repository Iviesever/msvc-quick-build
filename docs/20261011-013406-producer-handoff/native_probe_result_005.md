# Producer handoff prerequisite 005: closed native counterexample

Status: `CLOSED_CANDIDATE_DISQUALIFIED`.
Next product state: `CANDIDATE_DISQUALIFIED_REPLACEMENT_NOT_SELECTED`.
A1/A2/A4 and the real ordinary terminal-LINK positive handoff remain incomplete.
A3/A5/deletion remain disabled.

## Exact execution and evidence

- Commit: `7d3cdfab365f5b08d7b89cff55f826b03c4fafdb`.
- Tree: `b87db56e54cccf2fed67b75bcf8fb408353ada34`.
- [Preactivation registration](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6104881176).
- [Windows run 38107384644](https://github.com/Iviesever/msvc-quick-build/actions/runs/38107384644): workflow number 5, attempt 1; job 114375652660.
- Windows Server 2022 build 20348; runner image `20261004.326.1`; selected MSVC `14.44.35207`, SDK `10.0.26100.0`.

The reviewed source contains the actual one-shot fixture, runner and Job helper.
All twelve reviewed source paths exactly match the downloaded artifact. The ZIP
and all 26 members were completely written, file/directory fsynced and fully
read back by a separate process. Independent runtime review repeated the member
and source checks and verified the mutation ordering against the C++ code.

| Retained evidence | Bytes | SHA-256 |
| --- | ---: | --- |
| `pre-execution-review-005.json` | 19317 | `c7cd564b512794da0ecab9f88e88e4a3d58e73befb326a08f4a567a60ca38e94` |
| `producer-seal-005-38107384644-1.zip` | 372609 | `88f0aacacd6828a557926058651cbd0f73f61704c8bb1a5d8fbc2a1620302170` |
| Raw `native.stdout.bin` | 17208 | `5cf643dd4ec29483105d1648fd10bf4326e016c63399ffb25badab60b45b6330` |
| `runtime-review-005.json` | 10401 | `1dff0fe47e388348c5d59e83e116c1c0537ec47e0caa0e157888967f5ee41e45` |
| `attempt-005-closure.json` | 85793 | `71e4f1501a4af4f80f9368210a479d9ea1879aaa13e797e4a8fc4e6d60f53999` |

Canonical reviews and closure records are retained with the complete evidence
delivery. The original Windows ZIP/raw streams remain authoritative for their
bytes. Derived projections preserve integers outside JavaScript's safe range as
exact decimal strings. In particular, the original 004 FILETIME remains
`134361607508425601`; its separate correction is preserved without overwriting
the old closure, ZIP or raw JSON.

## Build admission actually succeeded

The temporary read-only vctip image pin acquired and released successfully.
Execute-open controls succeeded before acquisition, failed with sharing error
32 while the pin was held, and succeeded after release on the same physical
object. Its 335288 bytes matched the preflight hash; seven same-handle reads
included an observed EOF. No control executed the image and no pin error occurred.

The fixture compiler returned 0. Its owned Job observed two processes and zero
active members, without timeout, termination or errors. The expected LINK PDB
was absent. The admitted native fixture ran exactly once, returned 2, and its
one-process Job completed naturally without errors or termination. Therefore
this workflow failure records the planned native falsification result, not
another build-admission failure.

## Decisive attribute-writer counterexample

The old FILE_WRITE_ATTRIBUTES handle first changed the fixture file's last-write
time to `132537600000000000`, with successful independent readback. A separately
opened final handle then requested GENERIC_READ with only FILE_SHARE_READ.
RWH level 7 was granted and observed pending.

The old handle changed the time to `132537600010000000`. Before the final pin
was released, the controller observed the mutation thread completed, its call
succeeded, and the RWH request was still pending. Only then did it cancel/drain
the oplock and close the final pin. A later independent read verified the new
time. Raw JSONL lines 29-30 contain the completed/pending observation, line 31
begins cancellation, line 36 records pin closure, and line 40 verifies the value.
The proof uses thread completion and control-flow order, not coarse timestamps.

This disproves the selected final seal's exclusion of an already-open attribute
writer. It does not test EXE content changes, conditional ACL enforcement,
out-of-Job services or all possible replacement designs.

## Other cases keep their exact classifications

| Case | Observation | Classification / limit |
| --- | --- | --- |
| Baseline | RWH granted and pending | Positive primitive control |
| WRITE_ATTRIBUTES | Mutation succeeded while final pin and pending RWH remained held | `CANDIDATE_DISQUALIFIED` |
| WRITE_EA | Final read pin opened; RWH request failed with error 300 | `EXCLUDED_BY_RWH_REFUSAL`; mutation under a granted pending RWH was not exercised |
| Directory ADD_FILE | Final read pin failed with sharing error 32 | `EXCLUDED_BY_PARENT_PIN` |
| Directory DELETE_CHILD | Old handle and final read pin coexisted | `INCONCLUSIVE`; no namespace mutation or ACL revocation tested |
| Directory TRAVERSE | Old handle and final read pin coexisted | Positive compatibility control |

The artifact stores file contents and raw observations. Extraction is not an
independent NTFS EA, ACL or timestamp snapshot.

## Closed attempts and current next gate

001 remains closed at source admission. 002-004 remain closed at the build
boundary with all original errors. 004 identified one live owned-Job snapshot
member as vctip; it did not identify the unknown historical members in 002/003.
005 is now closed with the valid WA counterexample. Total prerequisite spend is
five workflow runs, four compiler-driver invocations, eleven observed compiler
Job processes and one native fixture invocation. No ordinary MQB producer,
consumer/cost study, ETW or service-ownership study ran. No retry or 006 is
allocated by this result.

The next gate is a concrete writer-capability creation and lifetime boundary,
including legitimate IPC delegation. It must preserve actual ordinary compile,
late routing, terminal LINK, parent/cache completion and continuous object
handoff. It must explain initial CREATE_NEW capabilities and existing handles;
later ACL revocation or a pending RWH cannot waive the observed WA failure.

WRITE_RESTRICTED with a unique restricting SID remains `NOT_SELECTED`: it can
restrict writes while preserving ordinary input/tool reads, but a complete
argument for NULL-DACL endpoints, read-only IPC delegation and relevant ALPC
access mappings is missing. These are unresolved paths, not newly observed
bypasses. A new attempt needs a specified access/descriptor/impersonation
control for the actual mechanism before registration. Repeating 005 cannot
qualify a replacement.

The original acceptance contract and integration plan remain in force.
PR250 HOLD/Draft, PR248's independent draft, frozen cost preparation, historical
study failures and disabled M1b jobs remain unchanged. This continuation does
not merge a product feature or complete #198.
