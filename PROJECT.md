# Project

## Desired outcome

Elementize is a WordPress plugin that gives a Custom GPT controlled API access to WordPress, Elementor, and the Essentials/Pixfort template library.

Eventually the GPT should be able to:
- list, inspect, create, draft, update, and safely remove/manage WordPress/Elementor pages;
- edit Elementor copy, media, colors, icons, and supported settings;
- browse Pixfort sections/pages and preview metadata;
- insert selected Pixfort templates;
- use visual references to plan a page and choose matching Pixfort sections;
- safely rewrite copy that lives inside Pixfort child documents without modifying shared/original Pixfort templates.

## Fast V0.1 — complete

- [x] WordPress sidebar/status page.
- [x] Application Password authentication.
- [x] Page discovery and Elementor detection.
- [x] Structured copy extraction.
- [x] Targeted copy updates through Elementor's document model.
- [x] Malformed/stale-write rejection.
- [x] Visual layout/editor integrity after writes.
- [x] Real pre-change WordPress/Elementor revision before writes.

## Fast V0.2 — Pixfort library/composition

- [x] Inspect installed Pixfort Core 4.1.3 + Essentials 4.1.1 source.
- [x] Direct searchable/paginated Pixfort catalogue implemented and runtime-proven: 983 unique sections and 150 unique pages.
- [x] Direct template preparation/insertion through Pixfort/Elementor runtime proven.
- [x] Guarded insertion positions `start`, `end`, `before`, `after` proven.
- [x] Guarded top-level section/container removal proven.
- [x] Safe blank Elementor draft creation and create → compose workflow proven.
- [x] Pixfort visual probe implemented and proven with up to four candidate thumbnails and stable identity hashes.
- [x] Custom GPT visual comparison and autonomous reference-image matching proven.
- [x] First autonomous Custom GPT draft build, Pixfort insertion, copy read/write, revision creation, and visual verification proven.

## Fast V0.3–0.5.2 — guarded editing and real-use workflows

- [x] Guarded visual-setting writes proven for explicit colors, Pixfort icons, and existing WordPress image attachments.
- [x] Visual CSS regeneration after writes proven.
- [x] Compact/filterable visual-setting reads added and proven through the Custom GPT.
- [x] Elementize-managed lifecycle control proven end-to-end for trash, restore, publish, and unpublish; hard delete remains unavailable.
- [x] Guarded page-layout control proven end-to-end: `site` maps to `elementor_theme`; `standalone` maps to `elementor_canvas`.
- [x] Autonomous landing-page build proven with layout selection, multi-section Pixfort composition, offer-specific copy, and draft-only behavior.
- [x] Custom GPT schema reached 17 actions with explicit inline schemas for compatibility.
- [x] Authenticated image-only Media Library lookup added and runtime-proven.
- [x] Filename lookup tolerates WordPress format conversion such as uploaded JPG → stored AVIF.
- [x] Full natural-language media workflow proven on draft page 951865.

## Fast V0.5.3–0.5.13 — broad rewrite diagnostics and embedded Pixfort copy

Broad rewrite testing exposed visible Pixfort demo copy that was not present in the main page text surface. The investigation deliberately used read-only diagnostics before adding writes.

- [x] Main text audit, effective widget settings audit, rendered HTML audit, post-content reversible test, render-cache audit, and post identity diagnostic added during source tracing.
- [x] Root cause identified: page 951865 referenced separate published `pixfort_template` child documents through `pix_template_id` settings.
- [x] Read-only embedded-document graph added.
- [x] Revision/autosave references separated from genuinely independent live references.
- [x] Safety proof on page 951865: child templates 951871, 951874, and 951877 had `independent_external_reference_count: 0`; their other 27 references each were historical page revisions.
- [x] Safe strategy chosen: never mutate original Pixfort templates; clone them for the managed draft and relink only that page.
- [x] 0.5.12 first implementation exposed a PHP namespace escaping syntax error. Recovery build disabled the broken module and restored the site without page-data changes.
- [x] Embedded implementation rebuilt as a fresh PHP-linted module and enabled as 0.5.13.
- [x] 0.5.13 `/status` runtime proof passed with clone/relink enabled, direct template mutation disabled, owned-clone-only embedded writes enabled, and PHP-lint status true.
- [x] Fresh 0.5.13 graph audit re-proved the three-document isolation conditions.
- [x] Guarded clone → relink succeeded on page 951865: source templates 951871/951874/951877 cloned to 952000/952001/952002, page revision 952003 created, and originals verified unchanged.
- [x] Embedded text read proved all three clones were page-owned and writable with `prepare_required=false`.
- [x] Single-field embedded write runtime test passed and visually rendered on the frontend.
- [x] Full copy cleanup passed independently for all three owned clones, with revisions and exact post-save verification.
- [x] Visual frontend verification passed for Built-in, Freestanding, and Compact dishwasher tabs.
- [x] Final read-only sweep returned `legacy_demo_match_count=0`, `embedded_document_count=3`, `all_embedded_documents_writable=True`, and `prepare_required=False`.
- [x] Original Pixfort template writes remained zero throughout the embedded-copy writes.
- [x] Obsolete broken 0.5.12 embedded module removed from the repository after 0.5.13 proof.
- [x] WP Builder instructions updated to require embedded-document discovery, clone/relink isolation, owned-clone-only copy writes, and verification of both main-page and embedded copy layers.
- [x] Custom GPT schema updated to 0.5.13.0 and 21 actions, exposing the four runtime-proven embedded operations with inline/ref-free schemas.

Known limitation discovered during the 0.5.13 test: the embedded text reader can currently expose some style/control token values such as `secondary-font` and `font-weight-bold` because their setting keys contain copy-like words. WP Builder instructions explicitly forbid editing these values. Backend classification hardening remains desirable but is not required to prove the clone/relink architecture.

## Current environment

- WordPress 7.0.3 at `mijn-ibp.local`.
- Elementor 4.2.1 / Elementor Pro 4.2.1.
- Pixfort Core 4.1.3.
- Essentials 4.1.1.
- Runtime-proven local plugin build: 0.5.13.
- Active development branch: `fastbuild/pixfort-library`.
- Current repository Custom GPT schema: 0.5.13.0 with 21 actions.
- Temporary public test tunnel hostname remains ephemeral and must not be treated as production configuration.

## Safety constraints

- Preserve Elementor structured data unless an operation deliberately changes it.
- Use WordPress/Elementor/Pixfort APIs for mutations rather than direct SQL.
- Structural and visual mutations require appropriate WordPress permission and stale-state protection.
- Mutations use fresh page/document hashes/tokens where applicable and create pre-change revisions.
- New page creation is draft-only and cannot silently publish; failed Elementor initialization is rolled back.
- Visual writes remain restricted to explicitly supported targets; globals, dynamic values, inactive controls, and arbitrary Elementor internals are protected.
- Lifecycle writes are limited to Elementize-managed pages and require fresh lifecycle state plus explicit confirmation.
- Layout writes are limited to Elementize-managed drafts and require fresh layout state plus explicit confirmation.
- Media replacement requires a real editable WordPress image attachment.
- Direct embedded writes to original Pixfort templates are blocked.
- Embedded clone/relink requires an Elementize-managed draft, fresh page title/status/content hash, the exact current embedded-document set and hashes, and explicit confirmation.
- Clone/relink verifies the originals remain unchanged and verifies the page references the new owned clones after save; failed verification attempts rollback/cleanup.
- Embedded text writes require page-owned clones, fresh page/document hashes, exact setting paths and expected values, explicit confirmation, post-save exact-value verification, and rollback on multi-document failure.
- Purchase keys, Application Passwords, cookies, nonces, HAR files, and proprietary theme/plugin source are never committed or returned by the API.
- Normal operation must not depend on browser automation.

## Important source/runtime findings

- Pixfort's AJAX endpoints are wrappers; Elementize can call the underlying PHP implementation directly.
- `Source_Pixfort::get_data()` replaces Elementor IDs, runs Elementor import processing, imports media/nested templates, and normalizes content against the target document.
- Essentials adds `pix_domain` + `purchase_key` request headers internally during template download; these are not exposed by the API.
- Pixfort child documents can be independently stored/rendered even when the main Elementor page text is already rewritten.
- WordPress revisions/autosaves must not be counted as independent live consumers when deciding whether an embedded Pixfort template is shared.
- Clone-and-relink is the safe page-specific strategy for embedded Pixfort copy: originals remain untouched and the managed draft receives owned clones.
- WordPress may convert uploaded image formats, so attachment discovery should not assume the uploaded extension survives.

## Autonomous GPT acceptance test — 2026-08-09

A fresh Custom GPT conversation using the 0.5.13.0 schema and updated instructions autonomously built draft page 952013 (`Dishwasher Store`) from a natural-language request, visually compared Pixfort candidates, produced dishwasher-specific main copy, kept the page in site layout and draft status, and reported a clean final copy verification without user-supplied IDs/hashes.

Result: copy/autonomy behavior is promising, but the overall acceptance test is NOT fully passed yet. Visual inspection showed clearly off-topic imported imagery remained: a generic software/UI mockup in the hero, fashion-model photography in the dishwasher category section, and a generic laptop/stock-person image in the closing CTA. This is a semantic visual-content failure even though the text was rewritten correctly. WP Builder must not call a build complete while it knowingly selected dominant imagery unrelated to the requested subject.

Instruction hardening added after this test: topical media relevance is now a hard criterion during Pixfort visual comparison. Candidates with clearly unrelated dominant photos/mockups must be rejected unless a safe relevant WordPress Media Library replacement is already available; otherwise prefer neutral, icon/text-led, or topic-relevant alternatives. Whole-page verification now also forbids claiming completion when known selected imagery remains off-topic.

## Current objective

Re-run the fresh autonomous Custom GPT acceptance test with the hardened visual-relevance instructions.

Acceptance condition: the user provides only the business/page request. WP Builder autonomously creates a managed draft, visually chooses Pixfort sections, rewrites main and embedded copy safely, verifies both copy surfaces, and does not leave clearly unrelated dominant imagery or fake proof in the selected sections. The user should not need to supply internal IDs/hashes or manually perform the embedded workflow.

## Later

- Harden copy-field classification so style/control tokens such as `secondary-font` and `font-weight-bold` are excluded at the backend surface.
- Improve icon discovery beyond reusing already observed Pixfort icon values.
- Improve whole-page brand-palette verification without falsely claiming protected/global styles were changed.
- Decide whether page-title mutation should be exposed separately from Elementor visible-copy editing.
- Replace Quick Tunnel with stable public HTTPS staging/production connectivity.
