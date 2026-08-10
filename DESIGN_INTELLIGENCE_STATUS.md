# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.16.0`.

## Runtime-proven foundation

- 0.7.x catalogue breadth, visual-probe tracking, template-structure inspection, and structural hardening
- 0.8.x page-level design audit plus calibration for palette usage, typography coverage, spacing rhythm, CTA style tokens, inherited-solid contrast attempts, and composition rhythm
- 0.9.x read-only real design-control discovery for typography, spacing, alignment, sizing, borders/radius, shadows, backgrounds, colors, responsive scopes, and explicit/global/dynamic classification
- 0.10.x compact GPT control plane plus GPT-safe design-settings response budgeting
- 0.11.1 guarded typed design writer is runtime-proven for an exact reversible spacing write on managed draft page 952239
- 0.12.1 short-lived signed remote rendering is runtime-proven on page 952239, including anonymous incognito rendering without local-network permission prompts
- 0.14.4 free local screenshot capture + local Ollama visual critique are runtime-proven on page 952239
- 0.15.0 annotated section localization is runtime-proven on page 952239, with 10 exact top-level Elementor section markers and a second local vision pass
- 0.15.1 localization consistency hardening is runtime-proven on page 952239
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## Hard project constraint: no paid dependencies

Elementize must not require paid screenshot, browser-rendering, vision, or AI APIs. A feature may use free/open local software, but core visual QA must not depend on an external paid account or subscription.

The experimental 0.13.0 Cloudflare Browser Rendering + Workers AI implementation violated this constraint and is superseded. Do not configure Cloudflare account/token constants for visual audit.

## Current visual QA architecture

The rendered loop currently proven is:

signed managed draft preview → local Chrome screenshot → local Ollama critique → annotated internal screenshot → consistency-hardened section localization.

The clean screenshot remains the source for visual judgment. The annotated screenshot is a second internal-only pass used only to map findings to exact top-level Elementor section IDs. Neither screenshot is persisted or exposed by the completion-audit response.

### Runtime evidence on page 952239

- clean screenshot capture: 1280×9000, local Chrome, bounded timeout, no paid service
- local `gemma3:4b` critique surfaced weak hierarchy, excessive/inconsistent whitespace, small body text, irrelevant imagery, CTA clarity issues, and disconnected section flow
- localization created 10 markers (`S1`–`S10`) mapped to exact top-level Elementor IDs
- 0.15.1 acceptance returned two consistency-valid high-confidence mappings: visual hierarchy → S1/S3 (`5eb92308`, `b987dcd`) and CTA design → S2 (`47e5d82d`)
- both returned `localization_status=localized`, `usable_for_repair_discovery=true`, and matching rationale marker cross-checks
- consistency summary returned `rejected_count=0` and `all_returned_mappings_consistent=true`

## 0.16.0 — Read-only repair-candidate discovery

Implemented; runtime acceptance pending:

- no new GPT Action; repair discovery augments the existing `getElementizePageCompletionAudit?include_visual=true` response
- only findings already marked `localization_status=localized` and `usable_for_repair_discovery=true` are considered
- discovery inspects only the exact localized top-level Elementor section IDs
- candidate sources are existing guarded design controls, active writable visual settings, and link targets only when the finding is actually link/destination-related
- design candidates are restricted to controls currently advertised as `writable_now=true`
- visual candidates are restricted to existing `active=true` and `writable=true` targets
- finding/category matching is conservative: typography findings map to typography controls, whitespace/rhythm to spacing, hierarchy to typography/spacing/alignment, CTA design to button-like guarded controls/colors, imagery to media targets, and link candidates only to link/destination/navigation findings
- exact element IDs, setting paths, current values, design-control fingerprints, top-level IDs, and writer routes are returned for planning
- fresh Elementor, visual-settings, design-settings, and links content hashes are cross-checked; discovery fails closed if the page changes during the read sequence
- candidates are advisory only; no writes are performed
- every returned plan explicitly requires a fresh exact read immediately before any later write, preserving existing expected-value/fingerprint/hash/revision/verification/rollback guards
- action-slot cost remains zero

### 0.16.0 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect `visual.repair_discovery`.

Acceptance requires:

1. `available=true`, `read_only=true`, `writes_performed=false`, and `source_hashes_match=true`.
2. Only consistency-valid localized findings appear.
3. Every returned repair candidate belongs to one of the exact localized top-level section IDs.
4. Design candidates have `writable_now=true` and an exact `control_fingerprint`.
5. Visual candidates, if any, are active existing writable targets.
6. CTA-design findings do not receive unrelated generic link-destination candidates.
7. No mutation occurs and the page remains a draft.

## Next phase

After 0.16.0 is runtime-proven, add guarded repair orchestration for one narrow finding class at a time. Start with a reversible design-control class such as typography or spacing, require a fresh candidate re-read before each write, make one bounded change, capture a new rendered screenshot, compare the before/after visual critique, and keep the change only when the intended finding measurably improves without creating new blockers. Media replacement and broader style changes should remain later phases until their candidate-selection quality is separately proven.
