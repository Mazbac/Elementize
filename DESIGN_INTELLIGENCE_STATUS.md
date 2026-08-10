# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.21.1`.

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
- PHP syntax lint and GPT Builder contract gates active

## Hard project constraint: no paid dependencies

Core visual QA must not require paid screenshot, browser-rendering, vision, or AI APIs. Free/open local software is acceptable. Paid Cloudflare Browser Rendering/Workers AI remain superseded and forbidden for core operation.

## Current visual QA architecture

signed preview → local Chrome screenshot → local Ollama critique → annotated localization → repair discovery → semantic grading → local CDP measurements → rendered correlation → bounded planning diagnostics → deterministic rendered observations.

Screenshots are not persisted or exposed. Raw DOM, signed preview URLs, and CDP endpoints remain internal. All observation/correlation/planning layers remain read-only and advisory.

## 0.20.1 runtime diagnosis on page 952239

The empty plan was explained without weakening a gate: the fresh correlation had zero planning-ready candidates. The exact S1 padding was measured but only supporting/context-only for the fresh hierarchy finding. This confirmed vision/localization variability rather than a deterministic planner bug.

## 0.21.0 — deterministic rendered observations

Runtime-tested but dependency failed on the first acceptance run.

The returned `visual.render_observations` object was safe/read-only but `available=false` with `reason=Rendered metrics are unavailable.` This does not show whether the already-proven CDP metrics failed at browser start, preview/target discovery, context stability, metrics evaluation, JavaScript evaluation, or another bounded stage because 0.21.0 did not propagate the dependency diagnostics.

No write occurred. Do not weaken observation or repair gates based on this failure.

## 0.21.1 — rendered-observation dependency diagnostics

Implemented; runtime acceptance pending.

Goal: when deterministic observations cannot run because `visual.render_metrics` is unavailable, surface the existing bounded CDP failure information directly in `visual.render_observations`.

- no new GPT Action and no schema/instruction change
- completion audit keeps `visual.render_observations`
- upgrades `observation_version` to `0.21.1`
- adds `render_metrics_dependency`
- propagates bounded dependency fields only: presence/availability, metrics version, provider, failure stage, bounded reason, ready state, stable poll count, navigation transient count, and section count
- signed preview URL, CDP endpoint, raw DOM, screenshots, and secrets remain unexposed
- if metrics are unavailable, observation reason now includes the underlying bounded metrics reason and `dependency_failure_stage`
- no retry or duplicate Chrome launch is added yet; first diagnose the actual dependency failure
- read-only; `writes_performed=false`, `automatic_write_allowed=false`

### 0.21.1 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect only `visual.render_observations`.

Acceptance requires:

1. `observation_version=0.21.1`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`.
2. `render_metrics_dependency` is present.
3. If metrics succeed, observations should surface the deterministic microtext/CTA/padding facts.
4. If metrics fail again, `dependency_failure_stage` and the bounded dependency `reason` must identify the exact CDP stage instead of only saying rendered metrics are unavailable.
5. No mutation occurs.

## Next phase

Use the 0.21.1 dependency result. If CDP failure is transient and isolated, harden/retry only that exact stage. If metrics succeed, continue the original deterministic observation acceptance. Do not create promoted repair targets or perform a repair experiment until the observation layer is stable.