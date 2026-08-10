# Design Intelligence status

## Current branch

`fastbuild/design-intelligence`

## Runtime-proven foundation

- 0.7.x catalogue breadth, visual-probe tracking, template-structure inspection, and structural hardening
- 0.8.x page-level design audit plus calibration for palette usage, typography coverage, spacing rhythm, CTA style tokens, inherited-solid contrast attempts, and composition rhythm
- 0.9.x read-only real design-control discovery for typography, spacing, alignment, sizing, borders/radius, shadows, backgrounds, colors, responsive scopes, and explicit/global/dynamic classification
- PHP syntax lint gate is active
- GPT Builder contract guard enforces <=8000 instruction characters and the compact Action budget

## 0.10.0 — GPT control plane

Implemented:

- new compact `getElementizePageState` action returning layout + lifecycle state together
- new compact `updateElementizePageState` action for `set_layout`, `trash`, `restore`, `publish`, and `unpublish`
- new `getElementizePageCompletionAudit` action combining the existing hardened quality audit with the calibrated design audit
- legacy WordPress REST layout/lifecycle/quality/design endpoints remain available internally and for compatibility; they are no longer separate GPT Action slots
- canonical core GPT schema reduced to the stable core operations
- generated GPT schema target is now 24 operations, leaving six slots below the current 30-operation editor ceiling
- WP Builder instructions use the compact state and completion actions
- CI rejects regressions above the 24-operation Elementize budget

## Acceptance gate

After installing 0.10.0 and refreshing the GPT Instructions + Actions Schema:

1. `getElementizeStatus` should report 0.10.0 and the control-plane flags.
2. GPT Builder should accept the schema with no 30-operation error.
3. `getElementizePageState` should return both layout and lifecycle state without modifying the page.
4. `getElementizePageCompletionAudit` should preserve the hardened quality payload and calibrated design payload.
5. `getElementizePageDesignSettings` should still expose the real page controls read-only.
6. Only after those reads pass should a state-mutation smoke test be considered.

## Next after acceptance

Use the runtime design-settings evidence from page 952239 to design the first typed guarded design writer. Do not expose a raw Elementor JSON writer. Add writer types only for control shapes and exact setting paths that were actually observed and validated.
