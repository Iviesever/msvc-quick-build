# Fixed-substitute analysis driver: requested handoff

The maintainer asked to check the latest handoff and complete its next step. This slice implements the next point recorded in [#198 / 6079598410](https://github.com/Iviesever/msvc-quick-build/issues/198#issuecomment-6079598410): connect PR251's capture component to a new analysis driver and freeze an independent protocol, using fixed substitutes to verify dual-pipe reads, timeout/termination, failed prefixes and final sealing.

The development base is main `d4c0d5f143ea52862329180512caa1cf5ffd0244`, tree `e4cbc65ea1db62d3735397417656b155eff89bf7`. PR251 is merged. PR250 remains Draft/HOLD at `a26b103685589ab8a5b4df60d1b760ec445efdf5`; study 001 remains failed-preflight and study 002 INVALID. Their original files, missing five observations and consumed budgets are preserved.

Acceptance requires a real, restricted Python substitute process exercising the new lifecycle; a wrapper that only supplies BytesIO would not test pipe backpressure or termination. Execution is Linux-only and does not run codec, MQB, MSVC, ETW, or a performance study. It grants no replacement allocation and changes no product authority, VERSION, existing workflow or historical evidence.

The maintainer's instruction to continue follows the displayed handoff and authorizes this implementation and the established GitHub Connector submission/review/normal-merge workflow. Routine implementation decisions are resolved within that accepted scope.
