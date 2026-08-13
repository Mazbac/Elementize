# Elementize development rules

Elementize is a guarded editing bridge for **existing Elementor + Pixfort pages**. It has two capability profiles: Standard editing (default) and optional page-scoped Creative Control. Safety is never disabled by either profile.

## Standard editing

Always in scope:
- Read existing Elementor pages and semantic structure.
- Resolve natural-language references and screenshot clues interpreted by ChatGPT.
- Reuse conversation selection context and repeated groups/cards.
- Read/write recognized safe text, links, Media Library images, and verified Pixfort icons.
- Search/import/deduplicate supported media.
- Custom GPT onboarding, connection status, Activity and guarded Undo.

Standard editing must not change page structure or design controls.

## Creative Control

Creative Control is explicitly enabled by an administrator for exactly one editable Elementor page at a time. The plugin, not GPT instructions, enforces the profile, page scope and capability revision.

When enabled for that page, Elementize may additionally:
- Search detected Pixfort-native and local Elementor templates.
- Inspect template structure/content/design controls before insertion.
- Insert an eligible template as local page elements.
- Remove, duplicate, move and reorder local page elements.
- Change only explicitly recognized local design controls such as color, spacing, radius, alignment, typography and size.
- Combine structural, design and normal content edits into one guarded creative transaction.
- Track Elementize-managed inserted roots and preserve that metadata through guarded Undo.

Creative Control uses templates as structural building blocks. The target page's observed design language is authoritative. Prefer existing palette/spacing/radius/typography tokens, consistent repeated components, simple hierarchy and bounded complexity. Do not create visually unrelated template collages.

Component Intelligence is read-only planning support. Rank candidates from fresh installed template data and the target page design language; never let a score bypass template inspection, Creative scope, stale-state guards, or rendered QA.

## Designer-agent behavior

For substantial visual work, read the page-wide designer context before writing. Treat page-coherence findings as conservative read-only evidence: compare like-for-like roles, respect evidence truncation, and never mutate merely because a heuristic fired. Blueprint v3 makes reference intelligence explicit and derives deterministic progressive build checkpoints from the section narrative. After a checkpoint anchor is implemented, no later section should be built until its exact native-vision review has run; at most one defensible local repair is allowed before continuing. Designer Context's visual critic rubric covers hero prominence, whitespace occupancy, media scale, composition repetition, CTA dominance and close/footer rhythm, but remains a ChatGPT-vision rubric rather than a server aesthetic score. The same conversation-owned blueprint carries page/conversion intent, responsive/interaction intent and the final acceptance plan/QA matrix. Reuse its exact fingerprint across ranking and `plan_context`; Activity stores only the compact verified trace. Templates remain implementation primitives; candidate choice should minimize structural mismatch, normalization cost, responsive risk, dependency risk and unnecessary complexity.

Rendered QA supports desktop/tablet/mobile. Add breakpoint-specific values only when a verified viewport problem requires them. Passive browser diagnostics are allowed because they are page-scoped and read-only; unrestricted navigation, form submission, crawling, or browser mutation remains out of scope.

## Still out of scope

Do not add page creation, publishing/unpublishing/trash/restore, unrestricted Elementor JSON writes, site-wide/global design mutation, Elementor dynamic-value writes, shared/global/embedded template mutation, Theme Builder/header/footer mutation, unrestricted browser automation, or autonomous aesthetic writes without grounded local controls.

Exact responsive Visual QA prefers the bundled no-package CDP runner with Node.js 22+. A fallback capture must expose `viewport_exact=false` and must never be described as exact breakpoint verification.

Rendered Visual QA is read-only and page-scoped. Elementize may capture a signed local Chromium render, settle transient motion, bound tall screenshots, and return a ZIP through the GPT Action `openaiFileResponse` contract. Handoff v2 prefers a short-lived opaque signed file URL on the configured public Elementize origin and verifies size/SHA-256 before serving; inline base64 is fallback only. Native ChatGPT vision performs the visual judgment after Data Analysis extracts `screenshot.png`; server-side `visual_analysis_verified=false` is expected for that handoff. Visual QA v11 may classify verified telemetry into deterministic quality gates and bounded repair signals, but every signal remains advisory (`safe_to_autorepair=false`): unlocalized evidence must be localized first and infrastructure failures must never trigger page mutations. Never expose preview URLs or claim visual verification without an actual inspected render.
## Mutation rules

Every mutation must fail closed and use fresh exact state. Never weaken these rules in Creative Control. For visual repairs, the intended visual scope must fit within the selected control's declared effective scope; reject broader controls for narrower repairs.

Content-only writes require fresh page identity/hash, exact target IDs/paths/current values, a pre-change revision, one verified save, persisted `_elementor_data` verification and rollback on failure.

Creative transactions additionally require:
- active Creative Control for the exact target page;
- exact `expected_capability_revision`;
- complete plan validation before save;
- in-memory structural mutation before touching persistence;
- unique Elementor element IDs and regenerated IDs for inserted/duplicated structures;
- preservation of unknown Elementor node properties;
- one pre-change revision and one Elementor save for the related transaction;
- exact persisted full-tree verification;
- verified managed-root metadata;
- Activity snapshot/record so the whole transaction can be undone safely.

Never blindly retry a mutation after an unknown result. Fresh-read first, recognize already-persisted desired state, and rebuild from fresh state before one bounded retry. Any successful batch/transaction invalidates the previous content hash.

Global style references and dynamic Elementor values remain read-only. Embedded/global template dependencies are not eligible for managed insertion. Pixfort icons must use exact installed-library values. Remote media must keep HTTPS, MIME, size, dimension and provenance safeguards.

## Template provider rules

Do not hard-code undocumented Pixfort IDs or database tables. Discover Elementor-registered template sources at runtime. Prefer detected Pixfort-native sources; local Elementor templates are a supported fallback. If a provider cannot return usable Elementor data, fail with a controlled error rather than guessing.

Template cloning must treat Elementor nodes as opaque structured data: preserve unknown fields, regenerate actual Elementor element IDs and repeater `_id` values, and remap known local ID references without treating media attachment IDs as element IDs.

## Repository rules

- `main` is the accepted minimal product state.
- Feature work stays on its requested branch until explicitly merged.
- Keep the runtime and GPT Action surface intentional and documented.
- Never commit credentials, Application Passwords, connection keys, nonces, purchase codes, tunnel secrets or local output.
- Update GPT schema/instructions whenever the public REST contract changes.
- Keep plugin version, GPT Action schema version and runtime contract synchronized.
- PHP syntax lint, contract tests and frontend build must pass before a change is considered complete.
- Do not describe deterministic structural checks as rendered visual QA.
