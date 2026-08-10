# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.20.1`.

## Runtime-proven foundation

- 0.7.x catalogue breadth, visual-probe tracking, template-structure inspection, and structural hardening
- 0.8.x page-level design audit plus calibration for palette usage, typography coverage, spacing rhythm, CTA style tokens, inherited-solid contrast attempts, and composition rhythm
- 0.9.x read-only real design-control discovery for typography, spacing, alignment, sizing, borders/radius, shadows, backgrounds, colors, responsive scopes, and explicit/global/dynamic classification
- 0.10.x compact GPT control plane plus GPT-safe design-settings response budgeting
- 0.11.1 guarded typed design writer is runtime-proven for an exact reversible spacing write on managed draft page 952239
- 0.12.1 short-lived signed remote rendering is runtime-proven on page 952239
- 0.14.4 free local screenshot capture + local Ollama visual critique are runtime-proven on page 952239
- 0.15.x annotated localization + consistency hardening are runtime-proven on page 952239
- 0.16.x read-only repair discovery + semantic actionability hardening are runtime-proven on page 952239
- 0.18.2 local Chrome DevTools Protocol rendered metrics are runtime-proven on page 952239
- 0.19.x rendered repair correlation + measurement-accuracy hardening are runtime-proven on page 952239
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## Hard project constraint: no paid dependencies

Elementize must not require paid screenshot, browser-rendering, vision, or AI APIs. Core visual QA may use free/open local software only. Cloudflare Quick Tunnel may be used only as a free public transport path for local development; paid Cloudflare Browser Rendering/Workers AI remain superseded and forbidden for core operation.

## Current visual QA architecture

signed managed draft preview → local Chrome screenshot → local Ollama critique → annotated internal screenshot → consistency-hardened section localization → read-only exact-target repair discovery → semantic candidate grading → loopback-only Chrome DevTools Protocol rendered measurements → exact rendered repair correlation → bounded value planning.

Screenshots are not persisted or exposed. Raw DOM, DevTools endpoints, and signed preview URLs remain internal. All repair planning remains advisory/read-only unless and until a separate guarded experiment is explicitly implemented and runtime-proven.

## Runtime evidence on page 952239

- clean screenshot capture: 1280×9000, local Chrome, bounded timeout, no paid service
- local `gemma3:4b` critique surfaced weak hierarchy, excessive/inconsistent whitespace, small body text, irrelevant imagery, CTA clarity issues, and disconnected section flow
- localization created 10 markers mapped to exact top-level Elementor IDs
- 0.18.2 returned stable CDP measurements for all 10 top-level sections
- deterministic evidence showed inter-section gaps of 0, substantial internal section padding, CTA style divergence across the page, and visible 12px microcopy in S8
- 0.19.1 correctly reports exact top-level S1 padding 80/0/0/0 as measured and the S1 background as `rgb(255, 255, 255)` while preserving semantic gating
- one fresh 0.19.1 run produced a direct S1 whitespace candidate with `causal_support=supported` and `bounded_value_planning_ready=true`
- a later 0.20.0 completion-audit run returned `available=true`, matching content hash, but `eligible_candidate_count=0`, `planned_change_count=0`, and no plan; no write occurred
- because the fresh visual/localization chain can vary between runs, the empty 0.20.0 plan must not be treated as proof of a planner bug without seeing which exact gate rejected the fresh candidates

## 0.17.x — dump-dom rendered metrics attempt

Runtime-tested and superseded. 0.17.0–0.17.2 stayed read-only but could not reliably return the internal metrics payload. CDP is the proven replacement.

## 0.18.x — Chrome DevTools Protocol rendered metrics

Runtime-proven at 0.18.2. It uses isolated local Chrome plus loopback-only CDP `Runtime.evaluate` to return bounded section/text/CTA/media measurements mapped to exact Elementor IDs. Raw DOM, signed preview URLs, and the CDP endpoint are not exposed.

## 0.19.x — rendered repair evidence correlation

Runtime-proven through 0.19.1. Exact rendered measurement is kept separate from semantic causal support. Supporting/weak controls stay blocked even when measured exactly. Only semantically direct, exactly measured, base-scope reversible spacing/typography controls can become planning-ready. Automatic writes remain disabled.

## 0.20.0 — bounded repair value planning

Runtime-tested, read-only, but the first acceptance run returned no plan.

Expected behavior remains conservative: at most one spacing/typography plan; exact hash/fingerprint/value preserved; spacing delta capped at 12px; font delta capped at 2px; no write.

Observed runtime result on page 952239:

- `planning_version=0.20.0`
- `available=true`
- `read_only=true`
- `writes_performed=false`
- `automatic_write_allowed=false`
- `correlation_hash_matches_current=true`
- `eligible_candidate_count=0`
- `planned_change_count=0`

This is safe behavior, but the response did not explain whether the fresh correlation had no planning-ready candidate or whether a later proposal precondition rejected it.

## 0.20.1 — bounded planning diagnostics

Implemented; runtime acceptance pending.

Goal: make an empty bounded plan diagnosable without weakening any gate or performing a write.

- no new GPT Action and no schema/instruction change
- completion audit keeps `visual.repair_plan` and upgrades its `planning_version` to `0.20.1`
- adds `planning_diagnostics`
- counts candidates seen, correlation-ready candidates, candidates passing core safety gates, and candidates passing value-proposal preconditions
- returns bounded rejection records with exact finding category, top-level ID, element ID, category/control, actionability, measurement/support state, stage, and machine-readable reason codes
- distinguishes three main empty-plan causes: fresh correlation had no planning-ready candidate; a candidate failed a later core planning gate; or a candidate passed core gates but failed conservative proposal preconditions
- spacing diagnostics explicitly check finding semantics, px box shape, rendered box availability, large-internal-padding signal, inter-section-gap signal, and whether a >=60px current side matches the rendered box
- typography diagnostics explicitly check typography-like finding semantics, exact font-size property, and a measured small font-size sample
- diagnostics are read-only and keep `automatic_write_allowed=false`

### 0.20.1 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect only `visual.repair_plan`.

Acceptance requires:

1. `planning_version=0.20.1`, `available=true`, `read_only=true`, `writes_performed=false`, `automatic_write_allowed=false`.
2. `planning_diagnostics.enabled=true`.
3. If `plans=[]`, `planning_diagnostics.primary_diagnosis` must explain whether the fresh correlation itself had zero ready candidates or which later planner/proposal gate rejected the candidate.
4. Rejection records must remain bounded and must not expose secrets, signed preview URLs, raw DOM, or any write authority.
5. No mutation occurs.

## Next phase

Use the 0.20.1 diagnostic result to decide the next implementation. If the fresh correlation simply varies and often produces zero planning-ready candidates, add a deterministic rendered-observation/convergence layer rather than weakening the semantic gates. If a genuine planner precondition bug is identified, fix only that precondition. Do not perform the first actual repair experiment until bounded planning is stable and reproducible across fresh audits.