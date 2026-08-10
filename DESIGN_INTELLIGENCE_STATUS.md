# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.20.0`.

## Runtime-proven foundation

- 0.7.x catalogue breadth, visual-probe tracking, template-structure inspection, and structural hardening
- 0.8.x page-level design audit plus calibration for palette usage, typography coverage, spacing rhythm, CTA style tokens, inherited-solid contrast attempts, and composition rhythm
- 0.9.x read-only real design-control discovery for typography, spacing, alignment, sizing, borders/radius, shadows, backgrounds, colors, responsive scopes, and explicit/global/dynamic classification
- 0.10.x compact GPT control plane plus GPT-safe design-settings response budgeting
- 0.11.1 guarded typed design writer is runtime-proven for an exact reversible spacing write on managed draft page 952239
- 0.12.1 short-lived signed remote rendering is runtime-proven on page 952239
- 0.14.4 free local screenshot capture + local Ollama visual critique are runtime-proven on page 952239
- 0.15.0 annotated section localization is runtime-proven on page 952239
- 0.15.1 localization consistency hardening is runtime-proven on page 952239
- 0.16.0 read-only repair-candidate discovery is runtime-proven on page 952239
- 0.16.1 semantic repair-actionability hardening is runtime-proven on page 952239
- 0.18.2 local Chrome DevTools Protocol rendered metrics are runtime-proven on page 952239
- 0.19.0 rendered repair evidence correlation is runtime-proven on page 952239
- 0.19.1 correlation measurement-accuracy hardening is runtime-proven on page 952239
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## Hard project constraint: no paid dependencies

Elementize must not require paid screenshot, browser-rendering, vision, or AI APIs. A feature may use free/open local software, but core visual QA must not depend on an external paid account or subscription.

The experimental 0.13.0 Cloudflare Browser Rendering + Workers AI implementation violated this constraint and is superseded. Do not configure Cloudflare account/token constants for visual audit.

## Current visual QA architecture

The runtime-proven chain through 0.19.1 is:

signed managed draft preview → local Chrome screenshot → local Ollama critique → annotated internal screenshot → consistency-hardened section localization → read-only exact-target repair discovery → semantic candidate grading → loopback-only Chrome DevTools Protocol rendered measurements → exact rendered repair correlation with explicit property-measured semantics.

The clean screenshot remains the source for visual judgment. The annotated screenshot is an internal-only locator pass. Screenshots are not persisted or exposed by the completion-audit response. Rendered DOM measurements are returned as bounded structured evidence only; raw DOM, DevTools endpoints, and signed preview URLs remain internal.

### Runtime evidence on page 952239

- clean screenshot capture: 1280×9000, local Chrome, bounded timeout, no paid service
- local `gemma3:4b` critique surfaced weak hierarchy, excessive/inconsistent whitespace, small body text, irrelevant imagery, CTA clarity issues, and disconnected section flow
- localization created 10 markers (`S1`–`S10`) mapped to exact top-level Elementor IDs
- 0.16.1 correctly refused to manufacture an automatic edit when only generic supporting/weak controls were available
- 0.18.2 returned stable CDP measurements for all 10 top-level sections, including section geometry, computed spacing, text sizes, CTA styles, and media dimensions
- deterministic evidence showed inter-section gaps of 0, substantial internal section padding, CTA style divergence across the page, and visible 12px microcopy in S8
- 0.19.0 joined repair candidates with exact rendered evidence while keeping semantic actionability separate
- 0.19.1 correctly reports the S1 top-level `padding` box 80/0/0/0 as `property_measured=true`
- the same S1 padding candidate remains `context_only` for the visual-hierarchy finding because hierarchy only made it a supporting candidate
- exact S1 background color now reports the live rendered value `rgb(255, 255, 255)` with `property_measured=true`, but remains blocked because it is only supporting
- a fresh localized `whitespacemanagement` finding on S1/S2 produced one genuinely direct candidate: top-level S1 `padding`, current 80/0/0/0, exact rendered evidence, `causal_support=supported`, `bounded_value_planning_ready=true`, base scope, writable, while `automatic_write_allowed=false`
- all other direct spacing candidates in that finding remained blocked because their exact margin property was not measured
- 0.19.1 final totals: 10 exact rendered candidates, 1 causally supported candidate, 1 bounded-value-planning-ready candidate, and zero writes

## 0.17.x — dump-dom rendered metrics attempt

Runtime-tested and superseded. 0.17.0–0.17.2 stayed read-only but could not reliably return the internal metrics payload. Do not continue the dump-dom transport; CDP is the proven replacement.

## 0.18.x — Chrome DevTools Protocol rendered metrics

Runtime-proven at 0.18.2.

- no new GPT Action and no schema/instruction change
- isolated local Chrome/Chromium/Edge profile with loopback-only DevTools endpoint
- direct CDP `Runtime.evaluate` measurement of the live signed-preview page
- exact top-level Elementor ID mapping
- bounded section/text/CTA/media measurements only
- no raw DOM, signed preview URL, or CDP endpoint exposed
- no external account, API, or paid service
- 0.18.1 added execution-context stability and transient navigation retry
- 0.18.2 added safe DOM lookup plus bounded exception/failure-stage reporting; runtime acceptance passed

## 0.19.x — rendered repair evidence correlation

Runtime-proven through 0.19.1.

- no new GPT Action and no schema/instruction change
- completion audit adds `visual.repair_correlation`
- requires fresh matching Elementor content hash
- candidate-level exact rendered evidence is attached by Elementor ID/property
- `property_measured` is distinct from semantic causal support
- supporting/weak candidates never become direct merely because a nearby or exact property is measurable
- only a semantically direct, exactly measured, base-scope reversible spacing/typography design control can become `bounded_value_planning_ready=true`
- all candidates keep `automatic_write_allowed=false`
- 0.19.1 fixed composite top-level padding/margin measurement and exact top-level background-color measurement

## 0.20.0 — bounded repair value planning

Implemented; runtime acceptance pending.

Goal: convert at most one already planning-ready exact repair candidate into a conservative proposed value without performing a write.

- no new GPT Action and no schema/instruction change
- completion audit adds `visual.repair_plan`
- read-only; `writes_performed=false` and `automatic_write_allowed=false`
- requires 0.19.1 correlation with matching current Elementor hash
- accepts only already-direct, `property_measured=true`, `causal_support=supported`, base-scope, writable design controls with a control fingerprint
- first experiment budget is exactly one control
- supported first-plan categories are spacing and font-size typography
- spacing planning only occurs when the localized finding concerns whitespace/spacing/rhythm/balance/flow, inter-section gap is not the primary signal, large internal spacing is measured, current and rendered px values agree, and the affected side is at least 60px
- spacing delta is conservative: approximately 10% of the current side, minimum 4px and maximum 12px
- typography planning is limited to exactly measured small base-scope font sizes and a maximum +2px delta
- the plan preserves the complete current control value, changes only one component, records exact element/top-level IDs, setting path, fingerprint, source hash, rendered evidence, proposed value, delta, and a deterministic plan ID
- every plan explicitly requires a fresh exact control/hash/value/fingerprint re-read before any later write
- experiment requirements already specify revision creation, saved-value verification, fresh visual audit, fresh rendered metrics, before/after comparison, and rollback on write/verification failure or clear regression

### 0.20.0 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect `visual.repair_plan`.

Acceptance requires:

1. `planning_version=0.20.0`, `available=true`, `read_only=true`, `writes_performed=false`, and `automatic_write_allowed=false`.
2. `correlation_hash_matches_current=true`.
3. At most one plan is returned.
4. For the current specimen, the expected plan is the exact top-level S1 padding control (`element_id=5eb92308`, setting path `padding`, fingerprint preserved), because it is the only 0.19.1 bounded-value-planning-ready candidate.
5. Current value must remain 80/0/0/0 px and the proposal must change only the top side by a small bounded reduction. With the current deterministic rule, 80px should plan to 72px (`delta=-8`).
6. The plan must carry the same page content hash and explicitly require a fresh exact read before any write.
7. No mutation occurs.

## Next phase

After 0.20.0 is runtime-proven, implement the first one-change repair experiment as a separate explicit operation built on the existing guarded design writer. The experiment must freshly re-read the exact target, require the same expected value/fingerprint/hash, create a revision, apply only the planned change, verify the saved value, rerun screenshot + rendered metrics, compare before/after, and automatically restore the revision if verification fails or the measured/visual outcome clearly regresses. Do not generalize automatic repairs beyond this one reversible acceptance target until that loop is runtime-proven. Media replacement, CTA reference selection, and broader style normalization remain later phases.
