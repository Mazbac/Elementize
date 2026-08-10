# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

Current hardening line: `0.15.1`.

## Runtime-proven foundation

- 0.7.x catalogue breadth, visual-probe tracking, template-structure inspection, and structural hardening
- 0.8.x page-level design audit plus calibration for palette usage, typography coverage, spacing rhythm, CTA style tokens, inherited-solid contrast attempts, and composition rhythm
- 0.9.x read-only real design-control discovery for typography, spacing, alignment, sizing, borders/radius, shadows, backgrounds, colors, responsive scopes, and explicit/global/dynamic classification
- 0.10.x compact GPT control plane plus GPT-safe design-settings response budgeting
- 0.11.1 guarded typed design writer is runtime-proven for an exact reversible spacing write on managed draft page 952239
- 0.12.1 short-lived signed remote rendering is runtime-proven on page 952239, including anonymous incognito rendering without local-network permission prompts
- 0.14.4 free local screenshot capture + local Ollama visual critique are runtime-proven on page 952239
- 0.15.0 annotated section localization is runtime-proven on page 952239, with 10 exact top-level Elementor section markers and a second local vision pass
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## Hard project constraint: no paid dependencies

Elementize must not require paid screenshot, browser-rendering, vision, or AI APIs. A feature may use free/open local software, but core visual QA must not depend on an external paid account or subscription.

The experimental 0.13.0 Cloudflare Browser Rendering + Workers AI implementation violated this constraint and is superseded. Do not configure Cloudflare account/token constants for visual audit.

## Current visual QA architecture

The first rendered loop is now proven:

signed managed draft preview → local Chrome screenshot → local Ollama critique → annotated internal screenshot → section-marker localization.

The clean screenshot remains the source for visual judgment. The annotated screenshot is a second internal-only pass used only to map findings to exact top-level Elementor section IDs. Neither screenshot is persisted or exposed by the completion-audit response.

### Runtime evidence on page 952239

- clean screenshot capture: 1280×9000, local Chrome, bounded timeout, no paid service
- local `gemma3:4b` critique surfaced weak hierarchy, excessive/inconsistent whitespace, small body text, irrelevant imagery, CTA clarity issues, and disconnected section flow
- localization created 10 markers (`S1`–`S10`) mapped to exact top-level Elementor IDs
- hierarchy localized to S1/S2, CTA clarity to S3/S4, and typography readability to S5
- the run also exposed one important model-consistency defect: the imagery rationale said the problem appeared in S1 while `section_markers` was empty; page-wide whitespace correctly returned no exact marker

## 0.15.1 — Localization consistency hardening

Implemented; runtime acceptance pending:

- cross-check marker names mentioned in the model rationale against the explicit `section_markers` field
- reject contradictory model output instead of converting it into exact Elementor IDs
- reject a marker assignment if the rationale simultaneously describes the issue as page-wide/unlocalized
- each finding now receives `localization_status`
- only `localization_status=localized` with medium/high confidence is marked `usable_for_repair_discovery=true`
- rejected mappings are cleared of exact Elementor IDs and downgraded to low confidence
- response includes counts for localized, page-wide/unlocalized, and rejected findings
- this layer remains advisory and cannot authorize writes

Runtime acceptance should re-run page 952239 with `include_visual=true` and verify that any recurrence of the S1-in-rationale/empty-marker contradiction is rejected rather than treated as a trustworthy localization.

## Next phase

After 0.15.1 localization consistency is runtime-proven, build repair-candidate discovery. For each medium/high-confidence, consistency-valid localized finding, Elementize should inspect only the mapped top-level section and return exact existing safe controls/media/link targets that could plausibly address the finding. This must remain read-only planning first; no visual finding may bypass the existing fresh-hash, fingerprint, expected-value, revision, verification, and rollback guards.
