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
- design-settings discovery now advertises `write_endpoint_available`, page eligibility, and truthful per-control `writable_now`

Runtime acceptance on page 952239:

1. Fresh discovery uniquely identified top-level section `5eb92308` base `padding` as an explicit writable `px` box.
2. Original value was top/right/bottom/left = `80/0/0/0`, unlinked.
3. Writer changed top padding only from `80` to `84`; revision `952321` was created.
4. Fresh discovery verified `84/0/0/0` with all other box fields unchanged.
5. A second fresh guarded write restored the exact original value; revision `952323` was created.
6. Final discovery verified `80/0/0/0`, unlinked, page still `draft`.
7. Final content hash returned to the original value after restoration.

This proves the first general-purpose design-control read → guarded write → verify → fresh read → guarded restore → verify loop.

## Current completion state

Design Intelligence now has runtime proof for:

- broad catalogue exploration signals
- visual template inspection
- template structural inspection
- calibrated deterministic page design audit
- exact real design-control discovery
- compact GPT control plane
- guarded typed design-setting writes

The deterministic design audit remains advisory-only until stronger rendered evidence and more runtime calibration justify design blockers.

## Next phase

Implement reference-aware rendered-page visual evaluation without weakening draft privacy.

Preferred direction:

1. Add a short-lived signed preview capability for Elementize-managed drafts rather than making drafts public.
2. Preserve the actual page/layout rendering as closely as possible; do not substitute structural metadata for visual truth.
3. Let WP Builder inspect rendered evidence for hierarchy, whitespace, balance, imagery/backgrounds, CTA treatment, section rhythm, and reference similarity.
4. Keep rendered critique advisory initially.
5. Only after runtime proof consider stronger design-completion gating or expanding the writer to additional control families such as sizing, border, or responsive variants.
