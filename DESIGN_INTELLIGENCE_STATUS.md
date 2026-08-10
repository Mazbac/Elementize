# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.19.1`.

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
- 0.16.0 read-only repair-candidate discovery is runtime-proven on page 952239
- 0.16.1 semantic repair-actionability hardening is runtime-proven on page 952239
- 0.18.2 local Chrome DevTools Protocol rendered metrics are runtime-proven on page 952239
- 0.19.0 rendered repair evidence correlation is runtime-proven on page 952239
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## Hard project constraint: no paid dependencies

Elementize must not require paid screenshot, browser-rendering, vision, or AI APIs. A feature may use free/open local software, but core visual QA must not depend on an external paid account or subscription.

The experimental 0.13.0 Cloudflare Browser Rendering + Workers AI implementation violated this constraint and is superseded. Do not configure Cloudflare account/token constants for visual audit.

## Current visual QA architecture

The rendered loop proven through 0.19.0 is:

signed managed draft preview → local Chrome screenshot → local Ollama critique → annotated internal screenshot → consistency-hardened section localization → read-only exact-target repair discovery → semantic candidate grading → loopback-only Chrome DevTools Protocol rendered measurements → exact rendered repair correlation.

The clean screenshot remains the source for visual judgment. The annotated screenshot is an internal-only locator pass. Neither screenshot is persisted or exposed by the completion-audit response. Rendered DOM measurements are returned as bounded structured evidence only; raw DOM, DevTools endpoints, and signed preview URLs remain internal.

### Runtime evidence on page 952239

- clean screenshot capture: 1280×9000, local Chrome, bounded timeout, no paid service
- local `gemma3:4b` critique surfaced weak hierarchy, excessive/inconsistent whitespace, small body text, irrelevant imagery, CTA clarity issues, and disconnected section flow
- localization created 10 markers (`S1`–`S10`) mapped to exact top-level Elementor IDs
- 0.15.1 acceptance returned consistency-valid localized findings with marker/rationale cross-checking and repair-discovery eligibility
- 0.16.0 acceptance returned `available=true`, `read_only=true`, `writes_performed=false`, and `source_hashes_match=true`
- 0.16.1 acceptance returned `automatic_write_allowed=false` globally, hierarchy spacing/alignment candidates downgraded to `supporting`, and CTA margin downgraded to `weak`
- the 0.16.1 run returned no `value_planning_ready` candidates, so the system correctly refused to invent an automatic edit
- 0.18.2 rendered metrics returned `available=true`, `read_only=true`, `writes_performed=false`, `provider=local_chromium_cdp`, `page_ready_state=complete`, `stable_poll_count=4`, `navigation_transient_count=0`, and 10 exact top-level section IDs
- the rendered measurement pass completed in about 2.23 seconds and returned section geometry, padding/margins, text sizes, CTA computed styles, and media dimensions/source filenames without exposing raw DOM or the CDP endpoint
- runtime evidence confirms several deterministic style facts that the vision pass only described qualitatively: section S1 renders a purple pill CTA (`rgb(86, 24, 143)`, radius `9999px`), S7 renders a purple radius-10 CTA, and S10 renders a green radius-10 CTA; visible microcopy in S8 renders at 12px; inter-section gaps are 0 while several sections carry substantial internal padding
- 0.19.0 correlation returned `available=true`, `read_only=true`, `writes_performed=false`, `repair_discovery_hash_matches_current=true`, six exact rendered candidate matches, zero causally supported candidates, zero bounded-value-planning-ready candidates, and `automatic_write_allowed=false`
- the 0.19.0 run correctly kept all hierarchy candidates `supporting`, the localized CTA margin candidate `weak`, and the localized typography finding without an invented repair target
- runtime also exposed a reporting accuracy issue: a composite top-level padding box was returned as exact rendered evidence but labeled `exact_element_but_property_unmeasured`; the same path did not populate exact top-level background-color evidence. This does not authorize a write, but it should be corrected before value planning.

## 0.17.x — dump-dom rendered metrics attempt

Runtime-tested and superseded.

The goal was to obtain deterministic rendered section geometry, computed spacing/typography, CTA styling, and media measurements before any bounded repair experiment. The first implementation used a signed-preview-only browser agent plus local Chrome `--dump-dom`.

Runtime result on page 952239:

- 0.17.0: Chrome returned a DOM dump but no internal metrics payload
- 0.17.1: synchronous bootstrap hardening did not change the result
- 0.17.2: direct `wp_footer` agent plus output-buffer fallback also did not change the result
- all three attempts stayed read-only and did not mutate the page

Conclusion: do not continue stacking DOM-injection workarounds. The dump-dom transport is superseded for rendered repair metrics.

## 0.18.x — Chrome DevTools Protocol rendered metrics

Runtime-proven at 0.18.2.

- no new GPT Action and no schema/instruction change
- supersedes the 0.17 dump-dom transport for completion-audit rendered metrics
- launches the already configured local Chrome/Chromium/Edge with an isolated temporary profile and `--remote-debugging-port=0`
- discovers the loopback-only DevTools endpoint and signed-preview page target
- connects directly to the page target over a local WebSocket
- uses CDP `Runtime.evaluate` with by-value results to measure the live rendered page directly
- maps rendered top-level sections back to exact Elementor IDs
- returns section top/bottom/height/width, computed margin/padding, section gaps, background color, bounded text/CTA/media samples, and nearest Elementor IDs
- DevTools endpoint, signed preview URL, and raw DOM are not exposed
- temporary browser profile/logs are removed and the browser process is terminated after the read
- loopback transport only; no external account, API, or paid service
- action-slot cost remains zero
- 0.18.1 added synchronous execution-context stability polling and navigation-transient retry after runtime exposed `Execution context was destroyed`
- 0.18.2 replaced fragile dynamically quoted selectors with safe `data-id` comparisons and surfaced bounded browser exception details; runtime acceptance then passed

## 0.19.x — rendered repair evidence correlation

0.19.0 is runtime-proven. 0.19.1 measurement-accuracy hardening is implemented; runtime acceptance pending.

Goal: join semantic repair discovery with deterministic rendered metrics without granting write authority.

- no new GPT Action and no schema/instruction change
- completion audit adds `visual.repair_correlation`
- operates only when both repair discovery and rendered metrics are available
- re-computes the current Elementor content hash and rejects stale correlation if it differs from the repair-discovery hash
- indexes rendered text, CTA, media, and top-level section metrics by exact Elementor ID
- attaches candidate-level rendered evidence for exact spacing, typography, border-radius, color, and media properties when measurable
- distinguishes `exact_element`, `exact_top_level`, `section_context_only`, and `none`
- semantic actionability remains separate from rendered measurement: exact evidence does not make a supporting/weak candidate direct
- a candidate may become `bounded_value_planning_ready=true` only if it was already semantically direct, its exact affected rendered property is measured, it is a reversible design spacing/typography control, and it is base-scope
- all candidates still return `automatic_write_allowed=false`
- 0.19.1 explicitly marks `property_measured`, recognizes numeric composite top-level padding/margin boxes as measured evidence, and measures exact top-level background color from section metrics
- 0.19.1 recalculates causal/planning counts after the measurement correction while preserving the semantic gate

### 0.19.1 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect `visual.repair_correlation`.

Acceptance requires:

1. `correlation_version=0.19.1`, `available=true`, `read_only=true`, `writes_performed=false`, and `automatic_write_allowed=false`.
2. `repair_discovery_hash_matches_current=true`.
3. The top-level S1 `padding` candidate reports `property_measured=true` with the rendered 80/0/0/0 box, but remains `causal_support=context_only` because its semantic actionability is only supporting.
4. The exact top-level background-color candidate reports `property_measured=true` and its live rendered background value rather than an empty evidence object, while remaining blocked by the semantic gate.
5. Weak/supporting candidates remain `bounded_value_planning_ready=false` even when their rendered property is measured.
6. `causally_supported_candidate_count` and `bounded_value_planning_ready_count` remain zero for this specimen unless a genuinely semantically direct candidate appears in the fresh run.
7. No mutation occurs.

## Next phase

Do not force bounded value planning when the acceptance specimen still has zero semantically direct, exactly measured repair candidates. After 0.19.1 is runtime-proven, add a read-only deterministic rendered-observation layer that can surface exact page-wide signals the vision/localization path may miss (for example page-wide CTA style divergence, measurable microtext, and unusually large internal padding) without declaring those observations to be defects by themselves. Only observations that converge with visual critique and map to an exact guarded control should be eligible to create a new direct repair candidate. The first actual repair experiment must remain a separate one-change guarded write followed immediately by fresh screenshot/metrics comparison and automatic rollback if the measured/visual result regresses or verification fails. Media replacement, CTA reference selection, and broader style changes remain later phases.
