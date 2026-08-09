# Design Intelligence status

## Runtime-proven through 0.8.1

Branch: `fastbuild/design-intelligence`

Proven on the live Essentials/Pixfort environment:

- catalogue summary sees 1,133 templates: 983 sections and 150 pages
- catalogue family names are useful only as advisory naming clues; they are not trusted as proof of visual uniqueness
- visual probes distinguish materially different compositions, hierarchy, density, imagery treatment, and style
- read-only template structure inspection works against live Pixfort data
- 0.7.1 hardening corrects CTA counts, separates interactive widgets from hard dependencies, adds alignment confidence, media hints, and coarse composition families
- 0.8.0 page design audit works read-only
- 0.8.1 calibration produces credible advisory signals for isolated palette accents, compact text, CTA style fragmentation, spacing rhythm, typography coverage, and composition rhythm
- deterministic design findings remain advisory-only; rendered visual inspection is still required for actual hierarchy, whitespace, background media, compositing, and reference similarity

## 0.9.0 — design-controls discovery pending runtime acceptance

Implemented:

- read-only `/pages/{id}/design-settings`
- exact Elementor/Pixfort setting paths for discovered design controls
- categories: typography, spacing, alignment, sizing, border radius, border, shadow, background, and color
- explicit vs global vs dynamic source classification
- responsive-scope classification
- value-shape and unit reporting
- future guarded-writer candidate hints without granting write permission
- filters by category, element, top-level section, responsive scope, offset, and limit
- GPT Action `getElementizePageDesignSettings`
- WP Builder instruction to inspect real design controls before attempting substantial normalization

Runtime gate for 0.9.0:

1. Verify `getElementizeStatus` reports the 0.9.0 discovery flags.
2. Run `getElementizePageDesignSettings` on page 952239 and inspect summary/category/source counts.
3. Inspect typography, spacing, CTA-related, and responsive controls with filters.
4. Confirm global/dynamic values are classified read-only and that writer candidates are only explicit observed controls.
5. Only after runtime validation, design a typed guarded writer from proven setting shapes. Do not expose arbitrary Elementor JSON writes.

Later phases:

- typed guarded design-settings writer with stale-value/hash protection, revisions, verification, rollback, and CSS regeneration
- rendered-page visual critique for hierarchy, whitespace, balance, imagery/backgrounds, reference similarity, and desktop/mobile quality
- combine content quality, technical quality, deterministic design audit, and rendered visual critique into the final completion model
