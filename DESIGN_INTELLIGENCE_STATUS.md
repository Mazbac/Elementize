# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.23.0`.

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
- 0.22.0 exact observation/control matching runtime-proven; initial page-wide promotion rule was intentionally rejected as too broad
- 0.22.2 section-specific semantic convergence hardening + dependency diagnostics runtime-proven
- PHP syntax lint and GPT Builder contract gates active

## Hard project constraint: no paid dependencies

Core visual QA must not require paid screenshot, browser-rendering, vision, or AI APIs. Free/open local software is acceptable. Paid Cloudflare Browser Rendering/Workers AI remain superseded and forbidden for core operation.

## Current visual QA architecture

signed preview → local Chrome screenshot → local Ollama critique → consistency-hardened annotated localization → repair discovery → semantic grading → local CDP measurements → deterministic observations → exact observation/control matching → section-specific semantic convergence hardening → focused exact-sample visual verification.

Screenshots are not persisted or exposed. Raw DOM, signed preview URLs, and CDP endpoints remain internal. All observation/correlation/planning/convergence/verification layers remain read-only and advisory.

## 0.21.1 deterministic observation proof on page 952239

The stable rendered-observation run produced three factual observation classes:

- three 12px microtext samples in top-level `67668c89`
- 11 CTA-like samples with 7 distinct rendered style signatures
- large internal padding samples with `max_inter_section_gap_px=0`
- exact padding samples included S1 `5eb92308` top 80px; `3f040c5b` top 60/bottom 80; `5b4c56b1` top/bottom 100; `1ef5ae9e` top/bottom 80; `cdfcafe` bottom 100
- no defects were asserted and no write authority was created

## 0.22.0 exact-control bridge proof

The bridge mechanically mapped rendered observations to exact fresh guarded Elementor controls. S1 `5eb92308` mapped to base-scope `padding`, current 80/0/0/0, exact rendered top 80px and an exact control fingerprint. Other large-padding controls also mapped. CTA divergence remained blocked pending a reference style.

The first promotion rule was too broad because one page-wide visual-convergence boolean promoted every matching large-padding sample. Those broad promotions were never allowed to write.

## 0.22.2 runtime result on page 952239

The section-specific semantic hardening worked as intended:

- `convergence_version=0.22.2`
- `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`
- rendered observations and CDP metrics were both available; ready state complete; stable polls 4; navigation transients 0; 10 sections
- 8 exact controls mapped before hardening and 6 broad padding targets were considered
- `promoted_target_count=0`, `demoted_target_count=6`
- S1 exact top padding 80px was demoted with `section_localized_but_semantic_category_indirect`; the same section was visually localized only for `hierarchy` and `typography`, not a direct spacing/whitespace issue
- the other exact padding targets were demoted with `no_section_specific_visual_support`
- CTA style divergence remained blocked with `requires_reference_style=true`

This is a successful safety result, not a failure: exact measurement and page-wide visual agreement are not sufficient to authorize a spacing repair. The general localization pass still does not isolate which measured padding side visibly causes the spacing problem.

## 0.23.0 — focused exact-sample visual verification

Implemented; runtime acceptance pending.

Goal: ask the local vision model a narrow causal question about only the exact measured samples that have strong deterministic/control evidence but lack section-specific semantic visual support.

- no new GPT Action and no schema/instruction change
- adds `visual.focused_verification`
- runs at priority 485 after broad exact control matching and before section-specific hardening
- only considers exact-control targets for large internal padding or microtext that do not already have a semantically direct medium/high-confidence localized finding in the same section
- candidate limit is 4
- candidates are tied to exact target IDs and annotated section markers
- prioritizes a candidate in a section that already has indirect visual localization before purely unlocalized sections, then higher measured values
- captures a fresh signed annotated screenshot locally with Chrome/Chromium/Edge; URL and screenshot are not exposed or persisted
- uses the configured local Ollama model only
- prompt explicitly forbids judging from the number alone and asks whether the exact named side/sample visibly causes the relevant spacing or legibility problem in context
- model must return exactly one bounded assessment per supplied target ID
- verdicts: `visibly_problematic`, `not_visibly_problematic`, `uncertain`
- confidence: low/medium/high
- direction must be `reduce` for a problematic padding target and `increase` for problematic microtext; inconsistent outputs cannot become focused support
- no pixel replacement value is requested or produced
- `direct_repair_candidates_created=false`, `value_proposals_created=false`, `automatic_write_allowed=false`
- action-slot cost remains zero

### 0.23.0 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect only `visual.focused_verification`.

Acceptance requires:

1. `verification_version=0.23.0`, `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`.
2. At most four exact candidates, each carrying target ID, marker, exact top-level/element IDs, property, component and rendered value.
3. Exactly one assessment per candidate.
4. No value proposal and no direct repair candidate.
5. A target may become `eligible_for_hardened_convergence_review=true` only when the focused verdict is `visibly_problematic`, confidence is medium/high, and the direction is semantically consistent.
6. The pass is allowed to say S1 80px top padding is not visibly problematic or uncertain. The purpose is to test causality, not force a planned edit.

## Next phase

If 0.23.0 produces one stable medium/high-confidence focused support target across fresh audits, add a read-only convergence hardening layer that can use that exact focused support as an alternative to general category localization while preserving the same fresh hash/fingerprint/control evidence. Then update bounded planning to consume only that hardened target. If focused verification says none of the measured padding samples visibly cause the whitespace problem, stop pursuing padding and extend deterministic observations to the actual visual cause instead. The first real write remains blocked until one exact target is reproducibly supported.