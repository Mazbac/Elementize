# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

## Runtime-proven foundation

- 0.7.x catalogue breadth, visual-probe tracking, template-structure inspection, and structural hardening
- 0.8.x page-level design audit plus calibration for palette usage, typography coverage, spacing rhythm, CTA style tokens, inherited-solid contrast attempts, and composition rhythm
- 0.9.x read-only real design-control discovery for typography, spacing, alignment, sizing, borders/radius, shadows, backgrounds, colors, responsive scopes, and explicit/global/dynamic classification
- 0.10.x compact GPT control plane plus GPT-safe design-settings response budgeting
- 0.11.1 guarded typed design writer is runtime-proven for an exact reversible spacing write on managed draft page 952239
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

## 0.12.0 — Signed rendered-preview foundation

Implemented; runtime acceptance pending:

- `getElementizePageState` now receives a `visual_preview` object for eligible managed drafts without consuming another GPT Action slot
- preview URLs are short-lived (10 minutes), HMAC-signed, tied to the exact current Elementor content hash, and invalidated by edits
- only Elementize-managed draft pages are eligible
- preview responses send no-store/no-cache and noindex/nofollow/noarchive headers
- invalid, expired, stale, unmanaged, or non-draft preview requests fail closed
- the configured public Elementize HTTPS origin is used so local WordPress sites can be previewed through the existing tunnel
- rendered HTML rewrites local home/site origins to the public origin; a small same-origin CSSOM pass also repairs local absolute asset references that remain in loaded stylesheets
- canonical redirects are suppressed only for signed preview requests
- automatic screenshot capture is explicitly **not** claimed yet; this release establishes private remote rendering first
- WP Builder instructions require truthful visual behavior: inspect the signed preview only if the runtime can actually see rendered visuals; otherwise request a screenshot instead of inferring appearance from HTML or the URL

### 0.12 runtime acceptance gate

On managed draft page 952239:

1. `getElementizeStatus` reports 0.12.0 and signed-preview capability flags.
2. `getElementizePageState` returns `visual_preview.available=true`, a short expiry, and a signed public HTTPS URL.
3. Opening that URL in a browser session that is **not logged into WordPress** renders the draft successfully.
4. Dominant images, Elementor/Pixfort CSS, fonts, and background assets load through the public origin rather than `mijn-ibp.local`.
5. The response is no-store/noindex and does not expose WordPress authentication.
6. After any Elementor change, the old signed URL fails as stale; a fresh state read issues a new working URL.
7. After expiry, the URL fails closed.

Do not call 0.12 rendered-preview support runtime-proven until these checks pass.

## Visual acceptance specimen

The full-page screenshot of page 952239 is intentionally useful as the first visual-evaluation specimen. Human inspection found issues that deterministic audits underweighted: excessive vertical whitespace, underscaled typography, weak hierarchy after the hero, inconsistent section widths, off-topic/inconsistent imagery, fragmented CTA styling, and disconnected section rhythm. Future rendered evaluation should surface these classes of problems without pretending subjective preferences are deterministic facts.

## Current completion state

Design Intelligence has runtime proof for:

- broad catalogue exploration signals
- visual template inspection
- template structural inspection
- calibrated deterministic page design audit
- exact real design-control discovery
- compact GPT control plane
- guarded typed design-setting writes

The deterministic design audit remains advisory-only until stronger rendered evidence and more runtime calibration justify design blockers.

## Next after 0.12 preview acceptance

Determine the safest route to automatic screenshot evidence. Prefer capability detection and a bounded local/headless renderer if the WordPress environment can support one safely; otherwise keep the signed preview plus user-supplied screenshot workflow. Do not add an external screenshot service or make drafts public by default. Rendered critique remains advisory initially.
