# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.22.2`.

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

signed preview → local Chrome screenshot → local Ollama critique → consistency-hardened annotated localization → repair discovery → semantic grading → local CDP measurements → deterministic observations → exact observation/control matching → section-specific semantic convergence hardening → dependency diagnostics.

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

However, `visual_convergence=true` belonged to the page-wide deterministic observation, not to each padding sample. Exact measurement + exact control mapping proves what a value is, but does not prove that every such value causes the visually criticized spacing problem. Do not feed all six 0.22.0 targets directly into bounded planning.

## 0.22.1 — section-specific semantic convergence hardening

Implemented and loaded, but the first runtime acceptance call did not reach the section-hardening decision because the fresh `render_observations` dependency was unavailable in that run.

Observed output:

- installed status confirmed `elementize_version=0.22.1` and all section-hardening capability flags
- `visual.repair_convergence` returned the base `convergence_version=0.22.0`
- `available=false`
- `promoted_targets=[]`
- reason: `Fresh rendered observations are required before convergence-to-control mapping.`

The hardening layer intentionally only post-processes an available base convergence object, so this response does not mean section-specific support failed. It means the upstream fresh rendered-observation dependency did not become available in this particular completion-audit run. No mutation occurred.

## 0.22.2 — convergence dependency diagnostics

Implemented; runtime acceptance pending.

Goal: make an unavailable convergence object explain the exact upstream rendered-observation/CDP dependency state without launching a duplicate expensive retry and without weakening section-specific semantic gates.

- no new GPT Action and no schema/instruction change
- post-processes `visual.repair_convergence` whether it is available or unavailable
- upgrades `convergence_version` to `0.22.2`
- adds bounded `render_observations_dependency`
- reports observation presence/availability/version/reason, observation dependency failure stage, observation counts, and the already-bounded nested rendered-metrics dependency summary
- propagates a top-level `dependency_failure_stage`
- distinguishes missing observations from present-but-unavailable observations
- does not start another Chrome or Ollama pass in the same request (`dependency_retry_performed=false`) because the full visual audit is already expensive and can approach response/time limits
- signed preview URL, CDP endpoint, raw DOM, screenshots, and secrets remain excluded
- preserves 0.22.1 section-specific semantic hardening whenever the upstream dependency succeeds
- `writes_performed=false`, `automatic_write_allowed=false`

### 0.22.2 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect only `visual.repair_convergence`.

If upstream observations succeed:

1. `convergence_version=0.22.2`, `available=true`.
2. The 0.22.1 section-specific fields remain present.
3. Broad 0.22.0 exact matches are filtered through same-section semantic localization before remaining promoted.

If upstream observations fail again:

1. `convergence_version=0.22.2`, `available=false`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`.
2. `dependency_failure_stage` is populated.
3. `render_observations_dependency` explains whether observations were missing or unavailable and carries the bounded rendered-metrics failure stage/reason when present.
4. No retry/mutation occurs.

## Next phase

If 0.22.2 succeeds and 0.22.1 reproducibly leaves one or a small number of section-verified targets, build a new read-only bounded planner that consumes only those hardened promoted targets. If the upstream visual/CDP chain remains intermittently unavailable, harden only the exact dependency stage reported by 0.22.2 rather than adding blind retries or weakening semantic gates. The first real write remains blocked until one exact target is reproducibly supported.