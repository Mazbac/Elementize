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

Page 952239 remains the first rendered-design acceptance specimen. Human inspection found issues that deterministic audits underweighted: excessive vertical whitespace, underscaled typography, weak hierarchy after the hero, inconsistent section widths, off-topic/inconsistent imagery, fragmented CTA styling, and disconnected section rhythm. Automatic rendered critique should surface these classes of problems without pretending subjective preferences are deterministic facts.

## 0.14.0 — Free local screenshot + local vision audit

Implemented; runtime acceptance pending:

- no new GPT operation; `getElementizePageCompletionAudit?include_visual=true` remains the opt-in entry point
- paid Cloudflare Browser Rendering and Workers AI dependencies are removed
- local Chrome/Chromium or Microsoft Edge is discovered on Windows/macOS/Linux; an explicit `ELEMENTIZE_CHROME_PATH` override is supported if needed
- PHP `proc_open` launches the browser headlessly against a fresh signed preview
- capture uses a bounded 1280px desktop viewport with a tall-page limit, then attempts to trim trailing background whitespace when GD is available
- screenshot bytes are kept only in a temporary local file/memory, bounded to 10 MB, hashed, and deleted after analysis
- local Ollama is the vision backend; default model is `gemma3:4b`, overridable via `ELEMENTIZE_LOCAL_VISION_MODEL`
- Ollama is contacted only on loopback (`127.0.0.1`/`localhost`); optional `ELEMENTIZE_OLLAMA_URL` is constrained to loopback
- screenshot analysis returns the same bounded advisory dimensions/findings structure as the prior experiment
- no external account, API token, paid API, screenshot upload, or screenshot persistence is required
- status exposes separate readiness for local browser capture and local Ollama vision so setup failures are diagnosable without secrets
- normal completion audits remain unchanged unless `include_visual=true`

### 0.14 runtime acceptance gate

On page 952239:

1. Install 0.14.0 and verify status reports provider=`local`, paid_services_required=false, external_account_required=false.
2. Verify local Chrome/Edge detection and `rendered_visual_capture_ready=true`.
3. If Ollama is not installed/running, verify the status degrades cleanly with `rendered_visual_ollama_reachable=false` and normal audits still work.
4. Install/start free local Ollama and pull `gemma3:4b`; verify `rendered_visual_model_available=true` and `rendered_visual_audit_configured=true`.
5. Call the completion audit with `include_visual=true`.
6. Verify screenshot capture succeeds, no signed URL or screenshot bytes are returned, and the temp screenshot is not persisted.
7. Verify the local critique flags multiple obvious specimen issues such as whitespace/rhythm, underscaled text, imagery relevance/cohesion, CTA fragmentation, or weak section balance.
8. Compare against the human screenshot review before trusting it for autonomous corrections.
9. Only then let WP Builder use guarded writers to fix high-confidence findings and re-run.

## Current completion state

Design Intelligence has runtime proof for catalogue exploration, visual template inspection, structural inspection, deterministic design audit, real design-control discovery, compact GPT control plane, guarded typed design writes, and secure signed remote draft rendering.

Free local screenshot capture + local visual critique are implemented but not yet runtime-proven. Deterministic and rendered design audits remain advisory-only.
