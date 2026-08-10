# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.22.0`.

## Runtime-proven foundation

- 0.7.x catalogue breadth, visual-probe tracking, template-structure inspection, and structural hardening
- 0.8.x page-level design audit plus calibration
- 0.9.x real design-control discovery
- 0.10.x compact GPT control plane
- 0.11.1 guarded typed design writer runtime-proven
- 0.12.1 signed remote draft rendering runtime-proven
- 0.14.4 local Chrome screenshot + local Ollama critique runtime-proven
- 0.15.x annotated localization + consistency hardening runtime-proven
- 0.16.x repair discovery + semantic actionability hardening runtime-proven
- 0.18.2 local Chrome DevTools Protocol rendered metrics runtime-proven
- 0.19.x rendered repair correlation + measurement hardening runtime-proven
- 0.20.1 bounded planning diagnostics runtime-proven
- 0.21.1 deterministic rendered observations + dependency diagnostics runtime-proven
- PHP syntax lint and GPT Builder contract gates active

## Hard project constraint: no paid dependencies

Core visual QA must not require paid screenshot, browser-rendering, vision, or AI APIs. Free/open local software is acceptable. Paid Cloudflare Browser Rendering/Workers AI remain superseded and forbidden for core operation.

## Current visual QA architecture

signed preview → local Chrome screenshot → local Ollama critique → annotated localization → repair discovery → semantic grading → local CDP measurements → rendered correlation → bounded planning diagnostics → deterministic rendered observations → read-only observation/control convergence.

Screenshots are not persisted or exposed. Raw DOM, signed preview URLs, and CDP endpoints remain internal. All observation/correlation/planning/convergence layers remain read-only and advisory.

## 0.20.1 runtime diagnosis on page 952239

The empty plan was explained without weakening a gate: the fresh correlation had zero planning-ready candidates. The exact S1 padding was measured but only supporting/context-only for the fresh hierarchy finding. This confirmed vision/localization variability rather than a deterministic planner bug.

## 0.21.1 runtime proof on page 952239

The second 0.21.1 audit succeeded after one oversized-response retry and returned stable deterministic observations from the same fresh CDP run:

- `observation_version=0.21.1`
- `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`
- metrics dependency present/available, provider `local_chromium_cdp`, ready state `complete`, stable poll count 4, navigation transient count 0, section count 10
- three measured 12px microtext samples in top-level section `67668c89`
- CTA style divergence: 11 sampled CTA-like elements, 7 distinct rendered style signatures
- large internal padding observation with max inter-section gap 0
- exact padding samples included S1 `5eb92308` top 80px; `3f040c5b` top 60/bottom 80; `5b4c56b1` top/bottom 100; `1ef5ae9e` top/bottom 80; `cdfcafe` bottom 100
- all three deterministic observations returned `visual_convergence=true`
- `defects_asserted=false` and `direct_repair_candidates_created=false`

This proves deterministic observations can remain stable even when fresh localization categories vary.

## 0.22.0 — rendered observation/control convergence

Implemented; runtime acceptance pending.

Goal: bridge stable rendered observations to exact guarded Elementor controls without depending on the fresh localization category and without creating write authority.

- no new GPT Action and no schema/instruction change
- completion audit adds `visual.repair_convergence`
- read-only; `writes_performed=false`, `automatic_write_allowed=false`, `defects_asserted=false`
- a target is promoted only when all three gates hold: deterministic rendered measurement exists, the independent visual critique converges on the same issue class, and a fresh exact guarded design control maps to the same Elementor element/property
- fresh Elementor content hash is computed and every internal design-settings read must return the same hash
- only base-scope, currently writable controls with a non-empty control fingerprint can map
- large-padding observations map only to an exact same-element `padding` control whose current px side agrees with the rendered side within 1px
- microtext observations map only to an exact same-element font-size control whose current base px value agrees with the rendered font size within 1px
- CTA style divergence is intentionally blocked from promotion because a reference CTA/style has not been selected; no normalization value is inferred
- promoted targets include exact top-level/element IDs, setting path, current value, fingerprint, rendered property/value, matched component, writer route, and promotion basis
- promotion is bounded to six targets and does not create a value proposal or approve an edit
- action-slot cost remains zero

### 0.22.0 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect only `visual.repair_convergence`.

Acceptance requires:

1. `convergence_version=0.22.0`, `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`, `defects_asserted=false`.
2. `page_content_hash` is present and fresh design-control reads do not report a stale hash.
3. At least the S1 80px top-padding observation should promote if the exact top-level padding control is still base-scope, writable, fingerprinted, and still equals the rendered 80px top value.
4. Any promoted target must say `promotion_status=promoted_read_only`, `exact_control_match=true`, and carry the exact current value/fingerprint/setting path.
5. CTA style divergence must remain in `blocked_observations` with `requires_reference_style=true` rather than generating a style edit.
6. No mutation occurs.

## Next phase

If 0.22.0 produces the same exact S1 padding promoted target across fresh audits, update bounded value planning so it can consume a reproducible observation-promoted exact control as an alternative to a localization-category-specific correlation. The planner must still be read-only and keep the same conservative delta rules. Only after that path repeatedly produces the same one-control plan should the first explicit guarded write/verify/screenshot/compare/rollback experiment be implemented.