# Cost-output semantic adapter and independent decision preparation

## Accepted request

Continue the next handoff recorded in issue #198 comment 6081083173 after PR252.
Complete a focused, reviewed, tested slice through the established Connector PR
and normal merge workflow. The user has already authorized continuation and
isolated development; no new integration menu is needed.

## Problem

The bounded driver verifies fixed bytes, record identities, original-pipe EOF and
process ownership. Those checks alone do not establish the business meaning of
cost output. In particular, prepared has no operation or instrumentation field,
and Python equality can confuse booleans with integers. An intact, six-line
stream can still describe an impossible or wrongly associated observation.

## Acceptance

- Derive a strict prepared/observation contract from the preserved cost producer
  and independent audit, including units, integer bounds, null modes, order,
  deterministic consumption and prepared relationships.
- Add positive and negative fixed substitutes. Validate semantics before the
  next call and again at the final fresh audit, with a sticky first failure.
- Keep the transport and capture implementation, the original eight fixture
  byte outputs and historical evidence unchanged.
- Freeze an independently reviewable preparation protocol that explicitly
  refuses real execution until the enumerated future decisions are resolved.
- Retain meaningful RED/GREEN evidence, review the exact final source, inspect
  actual applicable CI, merge normally, and update the unique next handoff.

## Scope and retained limits

The development base is main bf5df76079f224ea9bb5e7822be683cb3a2e4d45,
tree 46007c773858dd50653db0a49ce6861b15733fb2. PR250 remains Draft/HOLD at
a26b103685589ab8a5b4df60d1b760ec445efdf5, with its original product base
2762749b27fc89156b724d19e89cce84e388ef52. No codec/compiler/MQB/MSVC/ETW or
performance study execution, new sample allocation, study003, missing-five
refill, 001/002 reopening, version change or product authority is included.

Historical 001 remains failed preflight; its earlier 20 preparation processes
and 100 public API calls remain consumed. Historical 002 remains INVALID with
100 measurement processes, 1340 total public API calls, 99 prepared and 495
observations preserved. Missing values stay null. Searches are not repeated.
