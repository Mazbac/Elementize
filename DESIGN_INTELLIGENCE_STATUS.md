# Design Intelligence status

## 0.7.0 — pending runtime acceptance

Implemented on `fastbuild/design-intelligence`:

- read-only Pixfort catalogue summary endpoint
- category, subtype, and conservative template-family counts
- template-search augmentation with `design_family`, tracked usage, and novelty signals
- per-page Pixfort template insertion/removal history for soft reuse penalties
- visual-probe inspection history
- read-only structural inspection for up to 8 shortlisted Pixfort templates
- structural descriptors for top-level shape, element/widget counts, role counts, alignment, density, image prominence, color hints, dependency indicators, and structural signatures
- WP Builder instructions requiring an internal design brief, broad catalogue exploration, diverse visual shortlists, visual probes, and structural inspection before important section choices
- OpenAPI actions for catalogue summary and structural inspection

Not yet runtime-proven:

- catalogue totals and family signals against the live Essentials catalogue
- usage-history recording after insert/remove
- visual-inspection history recording
- Pixfort structure inspection against live remote template data
- refreshed wizard-generated schema acceptance with the two new actions
- autonomous build behavior under the new exploration rules

Next after runtime acceptance:

1. Build the first deterministic page design-audit layer.
2. Start with high-confidence signals: palette drift, typography consistency, repeated section composition, CTA style consistency, and resolvable contrast.
3. Runtime-test the audit on intentionally weak and strong pages before adding any new design-writing capability.
