# Runtime module map

`elementize.php` is intentionally a minimal WordPress plugin entry point. Runtime loading and initialization order live in `elementize-bootstrap.inc`.

`includes/` remains mostly flat during this cleanup pass so proven runtime load paths are not changed blindly.

The modules naturally fall into these groups:

## Core / WordPress control
Core REST behavior, admin display, lifecycle, page layout, media/chat media, post identity/content sync, embedded safety, onboarding, signed preview, GPT control plane, and status versioning.

## Content / quality
Text audit, page quality + hardening, and template structure.

## Design controls
Design intelligence + hardening, design audit + calibration, design settings, response budgeting, guarded design writes, and design-write discovery.

## Rendering / deterministic visual evidence
Visual rendering/filters/audit/localization, render audits/cache audits, render metrics/CDP, rendered observations, repair correlation, focused verification, convergence, and bounded repair planning.

## Aesthetic Brain / candidate selection
`elementize-aesthetic-*` modules: page-level aesthetic judgment, grounding/reassessment, A/B candidate scoring, transport/context hardening, multi-sample consensus, semantic shortlist, and semantic/visual resolution.

## Cleanup rule
Do not physically move runtime modules into subdirectories until their dependency/load graph is explicitly mapped. `elementize-bootstrap.inc` preserves the accepted require/init order, while `elementize-status-version.inc` also loads several Aesthetic Brain decorators. Path-only refactors must preserve initialization order and pass PHP lint plus a local WordPress runtime smoke test.
