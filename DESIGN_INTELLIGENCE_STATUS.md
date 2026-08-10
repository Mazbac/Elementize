# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.17.0`.

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
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## Hard project constraint: no paid dependencies

Elementize must not require paid screenshot, browser-rendering, vision, or AI APIs. A feature may use free/open local software, but core visual QA must not depend on an external paid account or subscription.

The experimental 0.13.0 Cloudflare Browser Rendering + Workers AI implementation violated this constraint and is superseded. Do not configure Cloudflare account/token constants for visual audit.

## Current visual QA architecture

The rendered loop currently proven is:

signed managed draft preview → local Chrome screenshot → local Ollama critique → annotated internal screenshot → consistency-hardened section localization → read-only exact-target repair discovery → semantic candidate grading.

The clean screenshot remains the source for visual judgment. The annotated screenshot is a second internal-only pass used only to map findings to exact top-level Elementor section IDs. Neither screenshot is persisted or exposed by the completion-audit response.

### Runtime evidence on page 952239

- clean screenshot capture: 1280×9000, local Chrome, bounded timeout, no paid service
- local `gemma3:4b` critique surfaced weak hierarchy, excessive/inconsistent whitespace, small body text, irrelevant imagery, CTA clarity issues, and disconnected section flow
- localization created 10 markers (`S1`–`S10`) mapped to exact top-level Elementor IDs
- 0.15.1 acceptance returned consistency-valid localized findings with marker/rationale cross-checking and repair-discovery eligibility
- 0.16.0 acceptance returned `available=true`, `read_only=true`, `writes_performed=false`, and `source_hashes_match=true`
- 0.16.1 acceptance returned `automatic_write_allowed=false` globally, hierarchy spacing/alignment candidates downgraded to `supporting`, and CTA margin downgraded to `weak`
- the 0.16.1 run returned no `value_planning_ready` candidates, so the system correctly refused to invent an automatic edit
- the localized typography finding had no exact currently guarded typography target, demonstrating fail-closed behavior rather than creating an explicit style control merely to make the automation possible

## 0.17.0 — Rendered repair metrics

Implemented; runtime acceptance pending:

- no new GPT Action and no schema/instruction change
- augments the existing completion audit only when `include_visual=true`
- issues a fresh short-lived signed preview internally and adds a signed-request-only `elementize_metrics=1` mode
- launches the already configured local Chrome/Chromium/Edge in an isolated temporary profile with bounded `--timeout` and `--dump-dom`
- injects an internal browser-side metrics agent only into the valid signed preview
- maps rendered top-level sections back to exact Elementor IDs
- returns section top/bottom/height/width, computed margin/padding, section gaps, and background color
- returns bounded visible text samples with exact nearest Elementor IDs and computed font-size/line-height/font-weight/color
- returns bounded CTA samples with exact nearest Elementor IDs and computed styling
- returns bounded media samples with exact nearest Elementor IDs, rendered dimensions, filename, and object-fit
- raw DOM is never returned, the signed preview URL is never returned, temporary DOM/log/profile files are deleted, and no page mutation occurs
- action-slot cost remains zero

Why this phase was added before bounded value planning: 0.16.1 correctly found zero semantically direct reversible targets in the latest specimen run. The next safe step is therefore stronger deterministic rendered evidence about the actual visual cause, not forcing a writable nearby control into an experiment.

### 0.17.0 runtime acceptance gate

On page 952239, call the completion audit with `include_visual=true` and inspect `visual.render_metrics`.

Acceptance requires:

1. `available=true`, `read_only=true`, `writes_performed=false`.
2. `section_count` is non-zero and rendered sections carry exact `top_level_element_id` values matching the page.
3. Section geometry and computed padding/margin/gap evidence are populated.
4. At least some visible text/CTA/media samples carry exact nearest Elementor element IDs plus rendered metrics.
5. `signed_preview_url_exposed=false`, `raw_dom_exposed=false`, and `raw_dom_persisted=false`.
6. No page mutation occurs and the page remains a draft.

## Next phase

After 0.17.0 is runtime-proven, join rendered metrics with semantic repair discovery. Upgrade a candidate to a direct bounded repair target only when the rendered evidence shows that the exact control plausibly causes the exact visual finding—for example an excessive rendered section gap corresponding to an explicit writable padding/margin control, or undersized visible text corresponding to an exact writable font-size control. Only then add conservative value planning and one-change reversible experiments followed by fresh screenshot comparison. Media replacement, CTA normalization, and broader style changes remain later phases until their reference/asset selection is separately proven.
