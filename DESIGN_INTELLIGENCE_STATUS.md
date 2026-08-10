# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.21.0`.

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

## Runtime diagnosis on page 952239

The 0.20.1 runtime result resolved the empty-plan question without weakening any safety gate:

- `planning_version=0.20.1`
- `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`
- `correlation_available=true`
- `correlation_planning_ready_count_reported=0`
- 8 candidates seen
- 0 correlation-ready candidates
- 0 candidates passed core planning gates
- 0 candidates passed proposal preconditions
- `primary_diagnosis=fresh_correlation_has_no_planning_ready_candidate`

The fresh run contained only hierarchy/supporting controls and a weak CTA-spacing target. The exact S1 padding remained measured but was only `context_only` for hierarchy. Therefore the planner was correct to return no proposal. The prior fresh run that had a `whitespacemanagement` finding and a direct S1 padding candidate demonstrates visual/localization variability rather than a deterministic planner bug.

## Current visual QA architecture

signed preview → local Chrome screenshot → local Ollama critique → annotated localization → repair discovery → semantic grading → local CDP measurements → rendered correlation → bounded planning diagnostics → deterministic rendered observations.

Screenshots are not persisted or exposed. Raw DOM, signed preview URLs, and CDP endpoints remain internal. All observation/correlation/planning layers remain read-only and advisory.

## 0.20.x — bounded repair planning

0.20.0 safely returned no plan on its first runtime acceptance run. 0.20.1 added diagnostics and proved that the fresh correlation itself had no planning-ready candidate. No planner gate should be weakened to force a result.

## 0.21.0 — deterministic rendered observations

Implemented; runtime acceptance pending.

Goal: surface stable page-wide measured signals even when a fresh vision/localization run changes categories, without declaring those measurements to be defects and without creating write authority.

- no new GPT Action and no schema/instruction change
- completion audit adds `visual.render_observations`
- read-only, `writes_performed=false`, `automatic_write_allowed=false`
- consumes the already-proven `visual.render_metrics` payload rather than relying on localization to notice every issue
- records visible text samples at or below 13px with exact top-level and element IDs
- records factual CTA style divergence when multiple rendered CTA signatures exist, including exact CTA element IDs and computed font/background/text/radius/padding tokens
- records top-level internal padding sides at or above 60px separately from inter-section gaps
- cross-checks each deterministic observation against the independent local visual critique and returns only a `visual_convergence` boolean
- explicitly returns `defects_asserted=false` and `direct_repair_candidates_created=false`; measurements alone are not defects and do not create repair candidates
- observation samples are bounded; no raw DOM, screenshot, signed URL, or CDP endpoint is exposed
- action-slot cost remains zero

### 0.21.0 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect only `visual.render_observations`.

Acceptance requires:

1. `observation_version=0.21.0`, `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`.
2. `deterministic_measurements_only=true`, `defects_asserted=false`, `direct_repair_candidates_created=false`.
3. The known rendered 12px text, CTA style divergence, and >=60px internal padding should be surfaced if they are present in the fresh CDP metrics.
4. `visual_convergence` may vary with the fresh Ollama critique, but deterministic measured observations should not disappear merely because localization selected different findings.
5. No mutation occurs.

## Next phase

If 0.21.0 is stable, add a separate convergence-to-control layer. It may promote an observation only when: the deterministic rendered measurement is present, the visual critique independently supports the same issue, and an exact guarded Elementor control maps to the same element/property. Promotion must remain read-only at first. The first actual repair experiment remains blocked until one exact target is reproducibly supported across fresh audits.