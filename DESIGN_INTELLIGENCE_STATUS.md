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
4. The initial 0.12.0 render exposed a browser private-network/local-device permission prompt, proving some origin variants still pointed local.
5. 0.12.1 added broader origin rewriting and reported `signed_visual_preview_private_network_hardening=true`.
6. A fresh 0.12.1 signed preview then opened directly in incognito with no local-network/device permission prompt.

This proves secure anonymous remote rendering sufficiently for the next headless-capture acceptance test. Expiry/stale-link behavior remains protected by the implemented guards and can receive additional destructive-free spot checks later.

## Visual acceptance specimen

Page 952239 remains the first rendered-design acceptance specimen. Human inspection found issues that deterministic audits underweighted: excessive vertical whitespace, underscaled typography, weak hierarchy after the hero, inconsistent section widths, off-topic/inconsistent imagery, fragmented CTA styling, and disconnected section rhythm. Automatic rendered critique should surface these classes of problems without pretending subjective preferences are deterministic facts.

## 0.13.0 — Opt-in automatic screenshot + visual critique

Implemented; runtime acceptance pending:

- no new GPT operation is added; `getElementizePageCompletionAudit` gains optional `include_visual=true`
- normal completion audits remain local/read-only and do not call external rendering/AI unless visual critique is explicitly requested
- a fresh signed preview is issued internally and never returned in the visual-audit payload
- Cloudflare Browser Rendering captures a bounded full-page JPEG from the signed preview
- screenshot bytes are kept in memory only, bounded to 8 MB, hashed, and discarded after analysis
- Cloudflare Workers AI receives the screenshot for a structured advisory design critique
- default model is `@cf/google/gemma-4-26b-a4b-it`, with an optional wp-config model override
- returned critique covers hierarchy, spacing/rhythm, typography legibility, visual cohesion, imagery relevance, CTA consistency, and section balance
- findings are advisory-only and never become design blockers by default
- provider/account credentials are read only from wp-config constants and are never returned by status or audit responses
- missing provider configuration degrades to `visual.available=false` without breaking the existing quality/design audit
- screenshot bytes/base64 are never returned to GPT and are not persisted
- `VISUAL_AUDIT_SETUP.md` documents configuration and the runtime acceptance flow

### 0.13 runtime acceptance gate

On page 952239:

1. Install 0.13.0 and verify status reports `rendered_visual_audit=true`.
2. Before credentials are configured, verify `rendered_visual_audit_configured=false` and a normal completion audit still works unchanged.
3. Configure Cloudflare account ID/token outside chat/source control and verify status changes to configured=true.
4. Refresh the GPT Actions schema so `include_visual` is available on `getElementizePageCompletionAudit`.
5. Call the completion audit with `include_visual=true`.
6. Verify `visual.screenshot.captured=true`, bounded screenshot metadata is returned, and no signed URL or screenshot bytes are exposed.
7. Verify `visual.available=true` and the structured critique identifies multiple obvious specimen issues such as whitespace/rhythm, underscaled text, imagery relevance/cohesion, or CTA/style fragmentation.
8. Compare the critique against the human screenshot review before trusting it for autonomous corrections.
9. Only after visual critique is credible should WP Builder use supported guarded writers to fix high-confidence findings and re-run the visual audit.

## Current completion state

Design Intelligence now has runtime proof for catalogue exploration, visual template inspection, structural inspection, deterministic design audit, real design-control discovery, the compact GPT control plane, guarded typed design writes, and secure signed remote draft rendering.

Automatic screenshot capture and visual critique are implemented but not yet runtime-proven. The deterministic and rendered design audits remain advisory-only.
