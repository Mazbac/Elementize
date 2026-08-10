# Elementize 0.25 — Guarded visual self-correction milestone

This milestone records the first runtime-proven end-to-end visual repair loop for an Elementize-managed Elementor draft.

## Proven loop

1. Capture the real draft in local Chrome/Chromium.
2. Run local visual critique with Ollama/Gemma.
3. Localize the visual issue to an exact top-level section.
4. Correlate screenshot evidence with deterministic rendered metrics.
5. Recover the exact writable Elementor design control and fingerprint.
6. Produce at most one bounded repair proposal.
7. Re-read the exact control, page hash, expected value, and fingerprint immediately before writing.
8. Save one guarded draft-only change with a WordPress revision.
9. Render and visually re-audit the changed draft.
10. Evaluate conservative keep/rollback gates.
11. Keep the repair only when all required gates pass; otherwise restore the exact previous value.

## Runtime acceptance completed

The acceptance specimen proved both branches of the decision loop:

- **KEEP path:** a bounded top-padding reduction rendered exactly as planned, reduced section height by the same amount, introduced no neighboring gap regression, cleared the same repair target from the promoted/planned state, and was retained on the draft.
- **ROLLBACK path:** the known previous padding value was deliberately reintroduced as a regression probe; deterministic rendered section height increased and the same visual target was re-promoted, so Elementize restored the accepted value and verified the restore.

The page remained a draft throughout. No publish operation was performed.

## Safety invariants retained

- Managed drafts only.
- No arbitrary Elementor JSON writes.
- Typed/allowlisted design controls only.
- Fresh page hash, exact expected value, setting path, and control fingerprint required before writes.
- Revision created before each guarded change.
- Fresh post-write read verifies the saved value.
- Maximum one change for the first repair experiment/acceptance path.
- Local Chrome/Chromium is the render source of truth.
- Local Ollama vision is advisory; deterministic metrics support causal verification.
- No paid rendering or vision service is required.
- No automatic publishing.
- Failed or regressed candidate states are restored to the exact prior value.

## Scope of this milestone

This milestone proves the guarded architecture and one real spacing repair family end-to-end. It does **not** mean arbitrary visual defects or every Elementor control can yet be repaired autonomously. Generalization to more repair families, stronger outcome scoring, and broader multi-section/page acceptance remains future work.
