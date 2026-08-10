# Runtime module map

`includes/` currently remains flat on purpose during the first cleanup pass so runtime load paths are not changed blindly.

The modules naturally fall into these groups:

## Core / WordPress control
`elementize-core.inc`, admin display, lifecycle, page layout, media/chat media, post identity/content sync, embedded safety, onboarding, signed preview, GPT control plane, status versioning.

## Content / quality
Text audit, page quality + hardening, template structure.

## Design controls
Design intelligence + hardening, design audit + calibration, design settings, response budgeting, guarded design writes, and design-write discovery.

## Rendering / deterministic visual evidence
Visual rendering/filters/audit/localization, render audits/cache audits, render metrics/CDP, rendered observations, repair correlation, focused verification, convergence, and bounded repair planning.

## Aesthetic Brain / candidate selection
`elementize-aesthetic-*` modules: page-level aesthetic judgment, grounding/reassessment, A/B candidate scoring, transport/context hardening, multi-sample consensus, semantic shortlist, and semantic/visual resolution.

## Cleanup rule
Do not physically move runtime modules into subdirectories until their loader/dependency graph has been explicitly mapped. `elementize.php` is the main loader, while `elementize-status-version.inc` also loads several Aesthetic Brain decorators. Path-only refactors must preserve initialization order and pass PHP lint plus runtime status checks.
