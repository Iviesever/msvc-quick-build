# 013: complete the reviewed offline compatibility handoff

## Problem and authorized scope

The consumed 012 observation located the frozen parser's version-1
UWOP_SET_FPREG reserved OpInfo rejection. The current user request continues
the reviewed `native_startup_handoff_013.md` plan from feature HEAD
`7622aa4d2757c082c4db12140cb71129b3e3511b`.

## Acceptance contract (PACT)

- Purpose: obtain one bounded, versioned offline structural observation of the
  exact retained image after the selected version-1 consumer compatibility delta.
- Acceptance: the only parser acceptance change is `(frame & 15) != 0 and
  (version == 1 or info == 0)` in opcode 3; all raw bytes and other guards stay.
  A complete index must carry explicit schema/policy metadata. A later refusal
  is retained and becomes a new handoff without another actual parser call.
- Constraints: frozen 011/012 files and image remain byte-identical; Linux pure
  Python only; no native build/run, objdump, symbols, new DLL, production edits,
  workflow changes, merge or release. At most one separately registered actual
  013 call; an exclusive consumed marker precedes the sole worker launch/GO.
- Tests/evidence: genuine synthetic RED/GREEN; unchanged 178-test baseline;
  independent full-source and complete-index output/memory reviews; immutable
  Connector readback and #198 registration before feature CAS and observation;
  complete report plus clean owner exit within the five-second admission limit.

The approved design is the historical 013 handoff; this issue records its
execution and does not broaden that design. Numeric full-index budgets are
implemented and independently proved before the invocation manifest is frozen.
