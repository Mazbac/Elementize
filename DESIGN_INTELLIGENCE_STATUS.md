# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

## Runtime-proven foundation

- 0.7.x catalogue breadth, visual-probe tracking, template-structure inspection, and structural hardening
- 0.8.x page-level design audit plus calibration for palette usage, typography coverage, spacing rhythm, CTA style tokens, inherited-solid contrast attempts, and composition rhythm
- 0.9.x read-only real design-control discovery for typography, spacing, alignment, sizing, borders/radius, shadows, backgrounds, colors, responsive scopes, and explicit/global/dynamic classification
- 0.10.x compact GPT control plane plus GPT-safe design-settings response budgeting
- 0.11.1 guarded typed design writer is runtime-proven for an exact reversible spacing write on managed draft page 952239
- 0.12.1 short-lived signed remote rendering is runtime-proven on page 952239, including anonymous incognito rendering without local-network permission prompts
- 0.14.4 free local screenshot capture + local Ollama visual critique are runtime-proven on page 952239
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## Hard project constraint: no paid dependencies

Elementize must not require paid screenshot, browser-rendering, vision, or AI APIs. A feature may use free/open local software, but core visual QA must not depend on an external paid account or subscription.

The experimental 0.13.0 Cloudflare Browser Rendering + Workers AI implementation violated this constraint and is superseded. Do not configure Cloudflare account/token constants for visual audit.

## 0.10.x — GPT control plane

Runtime-proven:

- `getElementizePageState` returns layout + lifecycle state together
- `updateElementizePageState` consolidates `set_layout`, `trash`, `restore`, `publish`, and `unpublish`
- `getElementizePageCompletionAudit` combines the existing hardened quality audit with the calibrated design audit
- legacy WordPress REST layout/lifecycle/quality/design endpoints remain available internally and for compatibility; they no longer consume separate GPT Action slots
- generated design-settings responses are compacted and capped for GPT safety, with paging via `next_offset`
- current GPT Action budget target is 25 operations, leaving five slots below the 30-operation editor ceiling

## 0.11.1 — Guarded typed design writer

Implemented and runtime-proven:

- `updateElementizePageDesignSettings` writes only managed drafts
- initial supported categories: `spacing`, `alignment`, `typography`, `border_radius`
- exact fresh page title/status/content hash required
- exact element ID + setting path + expected value required
- exact fresh `control_fingerprint` required
- explicit `confirm_design_write=true` required
- global/dynamic and complex composited controls remain blocked
- pre-change revision is created
- Elementor save is re-read and exact requested values are verified
- verification failure triggers an attempted rollback
- Elementor per-post CSS is regenerated
- design-settings discovery advertises `write_endpoint_available`, page eligibility, and truthful per-control `writable_now`

Runtime acceptance on page 952239:

1. Fresh discovery uniquely identified top-level section `5eb92308` base `padding` as an explicit writable `px` box.
2. Original value was top/right/bottom/left = `80/0/0/0`, unlinked.
3. Writer changed top padding only from `80` to `84`; revision `952321` was created.
4. Fresh discovery verified `84/0/0/0` with all other box fields unchanged.
5. A second fresh guarded write restored the exact original value; revision `952323` was created.
6. Final discovery verified `80/0/0/0`, unlinked, page still `draft`.
7. Final content hash returned to the original value after restoration.

This proves the first general-purpose design-control read → guarded write → verify → fresh read → guarded restore → verify loop.

## 0.12.1 — Signed rendered preview

Implemented and runtime-proven:

- `getElementizePageState` returns a `visual_preview` object for eligible managed drafts without consuming another GPT Action slot
- preview URLs are short-lived (10 minutes), HMAC-signed, tied to the exact current Elementor content hash, and invalidated by edits
- only Elementize-managed draft pages are eligible
- preview responses send no-store/no-cache and noindex/nofollow/noarchive headers
- invalid, expired, stale, unmanaged, or non-draft preview requests fail closed
- the configured public Elementize HTTPS origin is used so local WordPress sites can render through the existing public tunnel
- rendered output rewrites local WordPress origins, including escaped/encoded variants, to the public origin
- canonical redirects are suppressed only for signed preview requests
- WP Builder may expose a signed preview URL only when the user explicitly requests the current URL for manual preview testing

Runtime acceptance on page 952239:

1. `getElementizePageState` returned `visual_preview.available=true` and a signed HTTPS URL.
2. The URL rendered the managed draft in an incognito browser without WordPress authentication.
3. Elementor/Pixfort styling and dominant imagery rendered through the tunnel.
4. 0.12.1 broadened origin rewriting after an initial local-network permission prompt.
5. A fresh 0.12.1 signed preview then opened directly in incognito with no local-network/device permission prompt.

## Visual acceptance specimen

Page 952239 remains the first rendered-design acceptance specimen. Human inspection found issues that deterministic audits underweighted: excessive vertical whitespace, underscaled typography, weak hierarchy after the hero, inconsistent section widths, off-topic/inconsistent imagery, fragmented CTA styling, and disconnected section rhythm.

## 0.14.4 — Free local screenshot + local vision audit

Implemented and runtime-proven:

- no new GPT operation; `getElementizePageCompletionAudit?include_visual=true` is the opt-in entry point
- no paid screenshot, browser-rendering, vision, or AI service is required
- local Chrome/Chromium/Edge is used for rendered capture; local Ollama is the vision backend
- explicit `ELEMENTIZE_CHROME_PATH` is supported for environments where the browser executable is not auto-discovered
- local Ollama is constrained to loopback; default model is `gemma3:4b`
- Chrome capture uses an isolated temporary profile, deterministic `--timeout` capture, bounded 1280px desktop width, a 9000px tall-page limit, file polling, and cleanup
- screenshot bytes are bounded to 10 MB, hashed, used only locally, and deleted after analysis
- signed preview URLs and screenshot bytes are not exposed in the completion-audit response
- Ollama receives an explicit structured-output JSON schema, the same schema is grounded in the prompt, temperature is zero, and incomplete model output is rejected rather than silently defaulted
- rendered critique remains advisory-only
- normal completion audits remain unchanged unless `include_visual=true`

### Runtime acceptance on page 952239

1. Local PHP process execution was available.
2. Chrome was explicitly configured with `ELEMENTIZE_CHROME_PATH`; browser capture readiness became true.
3. Ollama + `gemma3:4b` were installed locally; Ollama/model/vision readiness and overall configuration became true.
4. Early capture attempts exposed Windows Chrome process/pipeline and wait-behavior issues; 0.14.1–0.14.3 hardened file polling, process isolation, and deterministic timeout capture.
5. A manual Chrome CLI acceptance test proved local headless screenshot capture independently.
6. 0.14.3 then captured the signed page automatically at 1280×9000 in about 2.3 seconds without returning or persisting the screenshot.
7. 0.14.4 captured the page again at 1280×9000 (1,453,235 bytes) in about 2.72 seconds and produced a structured local visual critique.
8. The critique independently surfaced the same major classes seen in human review: weak visual hierarchy, excessive/inconsistent spacing rhythm, small text, fragmented visual cohesion, irrelevant/inconsistent imagery, weak CTA prominence/consistency, and disconnected section flow/balance.
9. `visual_available=true` and `rendered_visual_truth_obtained=true` were returned while the page remained unmodified.

The first rendered visual QA loop is therefore runtime-proven: signed draft preview → local Chrome screenshot → local Ollama vision critique → structured advisory findings.

## Current completion state

Design Intelligence has runtime proof for catalogue exploration, visual template inspection, structural inspection, deterministic design audit, real design-control discovery, compact GPT control plane, guarded typed design writes, secure signed remote draft rendering, free local screenshot capture, and free local rendered visual critique.

The next phase is finding-to-control repair orchestration: map high-confidence rendered findings to exact safe Elementor controls/media/link targets, make guarded changes only where the existing writers support them, then re-render and compare before/after results. Deterministic and rendered design audits remain advisory-only; rendered findings must not directly bypass write guards.
