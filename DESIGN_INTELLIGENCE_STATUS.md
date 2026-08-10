# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.22.1`.

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
- 0.22.0 exact observation/control matching runtime-proven, but its first promotion rule was too broad for planning because one page-wide visual-convergence boolean promoted every matching large-padding sample
- PHP syntax lint and GPT Builder contract gates active

## Hard project constraint: no paid dependencies

Core visual QA must not require paid screenshot, browser-rendering, vision, or AI APIs. Free/open local software is acceptable. Paid Cloudflare Browser Rendering/Workers AI remain superseded and forbidden for core operation.

## Current visual QA architecture

signed preview → local Chrome screenshot → local Ollama critique → consistency-hardened annotated localization → repair discovery → semantic grading → local CDP measurements → deterministic observations → exact observation/control matching → section-specific semantic convergence hardening.

Screenshots are not persisted or exposed. Raw DOM, signed preview URLs, and CDP endpoints remain internal. All observation/correlation/planning/convergence layers remain read-only and advisory.

## 0.21.1 runtime proof on page 952239

The deterministic observation run succeeded with all three observation classes present and visually converged:

- three 12px microtext samples in top-level `67668c89`
- 11 CTA-like samples with 7 distinct rendered style signatures
- large internal padding samples with `max_inter_section_gap_px=0`
- S1 `5eb92308` top padding 80px
- `3f040c5b` top 60/bottom 80
- `5b4c56b1` top/bottom 100
- `1ef5ae9e` top/bottom 80
- `cdfcafe` bottom 100
- metrics dependency available through local CDP, ready state complete, stable poll count 4, navigation transient count 0, 10 sections
- no defects asserted and no repair candidate/write authority created

## 0.22.0 runtime result on page 952239

The exact control bridge worked mechanically:

- `convergence_version=0.22.0`
- `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`, `defects_asserted=false`
- exact S1 padding target promoted with base-scope `padding`, value 80/0/0/0, matching rendered top 80px, exact fingerprint, and fresh page hash
- exact additional padding controls also mapped for `3f040c5b`, `5b4c56b1`, and `1ef5ae9e`
- CTA style divergence remained blocked with `requires_reference_style=true`
- 8 exact control matches were found and the first six were promoted due the target budget

However, this exposed an important safety weakness: `visual_convergence=true` belonged to the page-wide deterministic observation, not to each padding sample. Because of that, every exact large-padding sample inherited the same positive visual signal. Exact measurement + exact control mapping proves what a value is, but does not prove that every such value causes the visually criticized spacing problem. Do not feed all six 0.22.0 targets directly into bounded planning.

## 0.22.1 — section-specific semantic convergence hardening

Implemented; runtime acceptance pending.

Goal: prevent page-wide visual agreement from promoting every matching rendered sample.

- no new GPT Action and no schema/instruction change
- post-processes `visual.repair_convergence`
- keeps the original exact rendered measurement, fresh hash, exact same-property control match, writability, and fingerprint requirements
- additionally requires a medium/high-confidence consistency-hardened localization record for the same top-level section
- that localized finding must also be semantically direct for the rendered observation type
- large internal padding requires a localized issue class containing whitespace, spacing, rhythm, balance, or flow
- hierarchy by itself does not directly authorize a spacing change; such a target is demoted even when the exact padding is measured
- microtext requires a localized typography/readability/legibility issue in the same section
- global `visual_convergence=true` alone is explicitly insufficient
- promoted targets become `promotion_status=promoted_read_only_section_verified`
- demoted exact matches are returned as bounded `blocked_targets` with machine-readable reason codes
- CTA reference-style block is preserved
- `defects_asserted=false` and `automatic_write_allowed=false`

### 0.22.1 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect only `visual.repair_convergence`.

Acceptance requires:

1. `convergence_version=0.22.1`, `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`.
2. `section_specific_visual_support_required=true`, `semantic_localized_support_required=true`, and `global_visual_convergence_alone_is_insufficient=true`.
3. `pre_hardening_promoted_target_count` should show the broad 0.22.0 matches from the same run, while `promoted_target_count` may be lower after section-specific semantic filtering.
4. Any surviving target must have `section_specific_visual_support=true`, `semantic_localized_support=true`, a bounded `localized_support` record, and `promotion_status=promoted_read_only_section_verified`.
5. Exact controls whose section is only localized for hierarchy or another indirect issue must move to `blocked_targets`, not remain promoted merely because they are large padding values.
6. CTA divergence stays blocked pending a reference style.
7. No mutation occurs.

## Next phase

If 0.22.1 reproducibly leaves one or a small number of section-verified targets, build a new read-only bounded planner that consumes only those hardened promoted targets and deterministically ranks at most one reversible spacing/typography experiment. If 0.22.1 commonly leaves zero because the general localization categories remain too variable, do not weaken this gate; instead add a focused local visual-verification pass for the deterministic observation samples so the model is asked directly which measured sections visibly exhibit the spacing/typography problem. The first real write remains blocked until one exact target is reproducibly supported.