# Explicit clean: current-output eligibility and refusal contract

## Request and accepted scope

The maintainer asked to continue the next handoff after PR255. The authoritative
handoff is [#198 comment6091785630](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6091785630),
with completed delivery in [comment6091873908](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6091873908).
This slice defines the bounded product contract for explicitly discarding
user-selected, known-rebuildable target outputs. It must identify a supported
positive path or the exact authority that prevents it today.

The actual starting main is `c96c75750bcdebb1e8e31f69841d33ec4752e175`, tree
`d049d9fc95071796e5232f79655f6735e7322f89`. PR255 is merged. The current #198
thread has 326 comments and no subsequent comment at intake. The five existing
open PRs are Draft; no competing clean contract was present. The isolated branch
is `docs/explicit-clean-contract-20261010`. The directory timestamp is UTC.

## Problem

The product can derive intended artifact paths and observe files, but those are
different facts from proving that the current file is an exclusively owned,
rebuildable output. `for_target` does not encode configuration or architecture.
A Debug/Release overwrite can therefore reuse a pathname. Existing recorded
results, known-write declarations and finite physical observations do not grant
deletion authority. Actual ordinary builders do not use the existing write-domain
admission primitive, whose started state deliberately lacks completion/reset.

The contract must avoid two unusable outcomes: a pathname-based deletion rule
that mistakes claims for authority, and a public command which can never admit
a positive case. It must supply a concrete producer-to-cleaner handoff and an
implementation dependency, while reporting honestly that no executor exists yet.

## Acceptance

1. Select a bounded positive profile with an exact target/configuration/input/
   effective-recipe/output binding. Distinguish a design success case from a
   presently available execution permission.
2. Define protected, shared, unknown, legacy and unsupported exclusions. A large
   file, extension, directory, age, missing target name or historical claim is
   insufficient. Preserve PDB/ILK and shared intermediate outputs in this first
   EXE-only profile and state the resulting space-recovery limitation.
3. Define separate eligibility, writer-completion, runtime-use and physical
   execution conditions; reject caller assertions, empty gap lists and process
   exit as substitutes for their responsible producers.
4. Bind preview and execution to the same live authority and physical objects;
   handle changed configuration/content, ancestor replacement, reparse points,
   hard links, partial errors, interrupted execution and uncertain outcomes.
5. Give explicit success/refusal scenarios and the next smallest implementation
   handoff. A scenario table is an acceptance specification, not executed tests.
6. Publish English/Chinese contracts and a machine-readable decision/source
   index, independently review their consistency, then use the authorized GitHub
   Connector workflow for the focused PR, actual applicable CI and normal merge.

## Preserved boundaries

The ordinary-generation consumer/cost decision remains DEFER. This slice does
not adopt its codec, reopen a study, create a measurement budget, run project
tests or real workloads locally, implement clean/prune, or delete outputs.
PR250 and #248 remain independent Draft/HOLD; frozen preparation, original
failures and missing evidence remain unchanged. #164 retains the broader
session/recovery roadmap. VERSION stays 5.6.0; #198 and the v5.7.0 Windows/Rium
and release gates remain open.

The maintainer's continuing instruction authorizes completion of this already
specified contract slice and its existing Connector delivery workflow. This
document is not a request for another approval of that same scope, and it does
not authorize a future native execution budget or a deletion operation.
