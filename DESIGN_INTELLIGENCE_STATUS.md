# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.16.1`.

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
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## Hard project constraint: no paid dependencies

Elementize must not require paid screenshot, browser-rendering, vision, or AI APIs. A feature may use free/open local software, but core visual QA must not depend on an external paid account or subscription.

The experimental 0.13.0 Cloudflare Browser Rendering + Workers AI implementation violated this constraint and is superseded. Do not configure Cloudflare account/token constants for visual audit.

## Current visual QA architecture

The rendered loop currently proven is:

signed managed draft preview → local Chrome screenshot → local Ollama critique → annotated internal screenshot → consistency-hardened section localization → read-only exact-target repair discovery.

The clean screenshot remains the source for visual judgment. The annotated screenshot is a second internal-only pass used only to map findings to exact top-level Elementor section IDs. Neither screenshot is persisted or exposed by the completion-audit response.

### Runtime evidence on page 952239

- clean screenshot capture: 1280×9000, local Chrome, bounded timeout, no paid service
- local `gemma3:4b` critique surfaced weak hierarchy, excessive/inconsistent whitespace, small body text, irrelevant imagery, CTA clarity issues, and disconnected section flow
- localization created 10 markers (`S1`–`S10`) mapped to exact top-level Elementor IDs
- 0.15.1 acceptance returned consistency-valid localized findings with marker/rationale cross-checking and repair-discovery eligibility
- 0.16.0 acceptance returned `available=true`, `read_only=true`, `writes_performed=false`, and `source_hashes_match=true`
- four localized findings were inspected and 10 exact existing candidates were returned
- hierarchy candidates were confined to localized sections and included exact writable spacing/alignment controls plus one active writable background color
- CTA consistency returned one exact `pix-button` spacing control and no unrelated generic link-destination candidate
- the localized typography finding correctly returned zero candidates when the current guarded writer exposed no matching typography target in that section
- imagery returned two exact active writable media targets in the localized section
- all returned design candidates carried exact `control_fingerprint` values and `writable_now=true`; media targets carried `active=true` and `writable=true`

The 0.16.0 run also exposed an important distinction: a target can be correctly localized and writable without being a semantically justified edit. For example, generic spacing/alignment can influence hierarchy without proving that the current value causes the hierarchy issue; CTA margin can move a button without directly resolving CTA style consistency. Candidate plausibility must therefore not be treated as write authority.

## 0.16.1 — Semantic repair-actionability hardening

Implemented; runtime acceptance pending:

- no new GPT Action and no schema/instruction change
- post-processes 0.16.0 candidates without mutating the page
- separates candidate `actionability` into `direct`, `supporting`, and `weak`
- keeps `automatic_write_allowed=false` for every candidate in this phase
- typography/readability only treats explicit font-size/line-height controls as direct bounded value-planning targets
- whitespace/spacing/rhythm only treats explicit padding/margin/gap controls as direct bounded value-planning targets
- hierarchy treats heading typography scale as direct; generic spacing/alignment is supporting only unless stronger evidence exists
- CTA color/style targets require a verified reference CTA/style before value selection; CTA spacing/alignment alone is weak for a consistency finding
- imagery media targets are direct target-identification evidence but remain blocked until a semantically relevant replacement asset is independently selected and verified
- link/destination candidates remain blocked until a real destination is freshly verified
- each candidate now reports semantic rationale, reference/asset/destination dependencies, `value_planning_ready`, and an explicit automatic-write block reason
- only direct reversible spacing/typography targets may advance to the next bounded value-planning phase

### 0.16.1 runtime acceptance gate

On page 952239, run the completion audit with `include_visual=true` and inspect `visual.repair_discovery`.

Acceptance requires:

1. `semantic_actionability_hardening.enabled=true` and `automatic_write_allowed=false`.
2. Generic hierarchy spacing/alignment candidates are no stronger than `supporting`.
3. CTA spacing/alignment candidates are `weak` for CTA consistency/style findings.
4. Imagery media candidates may be `direct` but must have `requires_replacement_asset=true` and `value_planning_ready=false`.
5. Only direct reversible spacing/typography controls may return `value_planning_ready=true`.
6. No page mutation occurs.

## Next phase

After 0.16.1 is runtime-proven, build bounded value planning for one reversible direct class at a time. Start with explicit spacing or typography controls only when the visual finding directly names that dimension. Generate a conservative proposed delta from the fresh current value, require another exact fresh read before writing, run one guarded reversible experiment, capture the page again, compare before/after visual evidence, and keep the change only when the intended finding improves without new blockers. Media replacement, CTA style normalization, and broader style changes remain later phases until reference/asset selection is separately proven.
