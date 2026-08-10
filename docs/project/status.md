# Elementize — Current Project State

## Product direction

Elementize is a guarded WordPress/Elementor/Pixfort design engine for AI agents.

The agent (ChatGPT, Claude/Cowork, or another orchestrator) should handle high-level intent and planning. Elementize should provide the specialized capabilities the agent cannot safely infer on its own:

- Elementor/Pixfort structural understanding;
- broad template discovery and candidate retrieval;
- rendered visual evidence and local visual judgment;
- exact guarded copy/media/design/page mutations;
- render → critique → repair verification;
- completion, quality, dependency, proof, and link auditing.

A future MCP surface is a likely integration direction, but current development continues through the existing guarded REST/control-plane architecture.

## Current accepted foundation

The following families are runtime-proven on local WordPress/Elementor/Pixfort test pages:

- guarded page/text/media/layout/lifecycle operations;
- Pixfort catalogue access, structure inspection, and section insertion;
- draft-only page composition and revisions;
- embedded Pixfort clone/relink safety for page-owned copy;
- page quality/completion audits;
- guarded design-control reads/writes with fresh hashes/fingerprints;
- signed/local preview rendering;
- local Chrome/Chromium/Edge screenshot and CDP measurements;
- local Ollama/Gemma visual critique;
- rendered observations, localization, repair discovery, bounded planning, keep/rollback verification;
- Aesthetic Brain page-level grounding and professional reassessment;
- independent candidate scoring with slot-order bias removed;
- anchored candidate discrimination;
- deterministic thumbnail-intensity correction;
- multi-sample visual consensus;
- structure-grounded semantic hero shortlisting;
- exact-state semantic/visual tie resolution;
- fail-closed selection below the visual quality floor.

## Current development line

Plugin header: `0.27.7`.

Design-intelligence submodules currently extend beyond the plugin header version, including the `0.27.9` visual-consensus and `0.28.x` semantic-selection work. Module versions are acceptance milestones; the plugin header should be bumped deliberately, not opportunistically during cleanup.

## Current blocker

Candidate retrieval quality for important Pixfort section selection.

For S1 on page `952239`, multi-sample consensus stabilized the current four semantic candidates below the selection floor (`5/5/4/4`). A previously tested `app-intro-left` candidate scored materially better in an earlier exact-context comparison but is missing from the current hardened semantic shortlist.

A read-only diagnostic exists to determine whether strong candidates are lost at:

1. metadata/discovery gating;
2. structural eligibility/scoring;
3. top-four/family-diversity selection.

Do not lower the visual quality floor merely to force a recommendation.

## Current cleanup branch

`cleanup/repository-structure`

Purpose:
- make repository structure understandable;
- keep the root limited to true repository/plugin entry files;
- separate current project docs, configuration, history, and local harnesses;
- map runtime modules before the physical PHP module-tree refactor;
- preserve exact runtime behavior while cleaning structure.

## Next milestones

1. Complete repository cleanup and local runtime smoke test.
2. Run the semantic-gate diagnostic on the cleaned branch.
3. Fix candidate retrieval so known strong candidates reach the visual ranker.
4. Build a wider structurally valid candidate beam and visually pre-screen it.
5. Prove final candidate-selection stability across repeated/order-permuted runs.
6. Connect accepted selection to the existing guarded section insertion workflow.
7. Perform a full autonomous landing-page acceptance test: brief → section selection → draft build → copy/media adaptation → rendered QA → bounded repair → final audit.
8. Generalize role-specific candidate retrieval beyond hero sections.
9. Later: expose a clean agent-agnostic/MCP interface over the proven Elementize engine.

## Non-negotiable safety constraints

- Never silently publish.
- Preserve managed-draft restrictions where applicable.
- Require fresh reads/state hashes/fingerprints before guarded mutations.
- Use revisions and exact post-save verification.
- Never expose unrestricted raw Elementor JSON mutation.
- Never mutate original/shared Pixfort template documents for page-specific copy.
- Reject unsupported proof/rating patterns.
- Never guess CTA destinations.
- Keep core visual QA free/local; paid rendering or AI services are not required for core operation.
- Do not commit credentials, Application Passwords, purchase keys, cookies, nonces, HAR captures, proprietary theme/plugin source, signed preview URLs, local audit output, or private runtime data.

## Working documents

- `docs/project/status.md` — this current project/roadmap snapshot.
- `docs/project/discovery.md` — durable integration/runtime findings.
- `docs/architecture/design-intelligence.md` — design-intelligence architecture and principles.
- `docs/history/` — superseded status/project snapshots and accepted milestone records.
- `config/gpt/` — active GPT instructions and OpenAPI material.
- `AGENTS.md` — repository operating rules.

## Progress convention

Use:
- **ACCEPTED** — runtime/CI-proven state;
- **CURRENT** — active blocker or acceptance milestone;
- **NEXT** — next concrete milestone;
- **PROGRESS** — percentage only when scoped explicitly.

Current scoped estimate: Aesthetic Brain / Design Intelligence foundation is approximately 99% complete. This is not an estimate for all of Elementize.
