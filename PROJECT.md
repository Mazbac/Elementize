# Project

## Desired outcome

Elementize is a WordPress plugin that gives a Custom GPT controlled API access to WordPress, Elementor, and the Essentials/Pixfort template library.

Eventually the GPT should be able to:
- list, inspect, create, draft, update, and delete WordPress/Elementor pages;
- edit Elementor copy, media, colors, icons, and supported settings;
- browse Pixfort sections/pages and preview metadata;
- insert selected Pixfort templates;
- use visual references to plan a page and choose matching Pixfort sections.

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
- [x] Identify direct catalogue path (`pixfort_elementor_library_data()`).
- [x] Identify direct template-preparation path (`Elementor\TemplateLibrary\Source_Pixfort::get_data()`).
- [x] Implement read-only searchable/paginated Pixfort catalogue.
- [x] Real-site catalogue test passed; catalogue has 983 unique sections and 150 unique pages.
- [x] Fix Pixfort Core detector and 0.2.2 bootstrap guard regression.
- [x] First Pixfort runtime insert proven visually and structurally on page 951706.
- [x] 0.2.4 copy hardening proven: layout/style keys no longer leak into the copy surface.
- [x] Guarded insertion positions `start`, `end`, `before`, `after` proven, including visual `before` ordering.
- [x] 0.2.5 guarded top-level section/container removal proven end-to-end.
- [x] 0.2.6 safe blank Elementor draft creation proven end-to-end.
- [x] Create → compose proven on page 951743.
- [x] Search → visual compare → choose → insert proven manually with `ai-agency-home-features-bento-boxes`.
- [x] 0.2.7 read-only Pixfort visual probe implemented at `POST /pixfort/visual-probe`.
- [x] Visual probe accepts 1–4 catalogue-backed section IDs, allowlists Pixfort thumbnail hosts, caps thumbnail bytes, returns A/B/C/D slots and stable `identity_hash` values, and packages images into `elementize-pixfort-visual-proof.json` for Code Interpreter.
- [x] Local visual-probe transport and image fidelity proven byte-for-byte by SHA256 for reconstructed A/B thumbnails.
- [x] Temporary Cloudflare Quick Tunnel exposes the local REST API publicly for GPT Action testing.
- [x] Custom GPT Basic authentication proven through the public tunnel.
- [x] Custom GPT `getElementizeStatus` action proven.
- [x] Custom GPT `searchPixfortPlannerCandidates` action proven.
- [x] Custom GPT `getPixfortVisualProbe` action proven with Code Interpreter reconstruction and visual A/B comparison.
- [x] Autonomous reference-image matching proven: GPT independently found `ai-agency-home-features-bento-boxes` from an uploaded reference image and mapped it back using identity hash `ea3a1325b3bff383f9fb76e4a85f366bd2464c8ef9102ed755a075df7c9239dd`.
- [x] Custom GPT write actions exposed for safe draft creation and Pixfort insertion.
- [x] First autonomous build proven: GPT created draft page 951750 and inserted visually selected `ai-agency-home-features-bento-boxes`, top-level ID `630fde86`, resulting hash `74987154ba79f59ef2128a5b3e9196f21afd4f0495ed88cdb44be24b86dd10d6`.
- [x] Visual proof passed in Elementor: page 951750 is a draft and renders the selected bento section correctly.
- [x] Custom GPT copy read action proven on page 951750: 32 text items returned, including heading/subtitle from widget `ba589ad`.
- [x] Custom GPT controlled copy write proven: exactly one heading changed to `ELEMENTIZE CONTROLLED COPY TEST`, revision `951754`, new hash `7ba7951ce8c715e0cf61e85180cbea9e4c5d303b77848af753b8c6dead48c2d9`.
- [x] Visual copy proof passed: heading changed, subtitle and layout remained intact.
- [x] 0.2.8 fixes the optional draft `slug` REST sanitizer bug by using an Elementize wrapper instead of passing `sanitize_title` directly as a multi-argument REST callback.
- [x] Runtime no-slug regression test passed: `POST /pages/create` with only title created Elementor draft page 951758 with no PHP error, `top_level_count: 0`, and empty-document hash `4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945`.

## Fast V0.3–0.5.2 — guarded editing and real-use workflows

- [x] Guarded visual-setting writes proven for explicit colors, Pixfort icons, and existing WordPress image attachments.
- [x] Visual CSS regeneration after writes proven.
- [x] Compact/filterable visual-setting reads added and proven through the Custom GPT; large-response failure resolved with `active=true`, `writable=true`, `compact=true`, `limit=20`, and pagination.
- [x] Elementize-managed lifecycle control proven end-to-end for trash, restore, publish, and unpublish; hard delete remains unavailable.
- [x] Guarded page-layout control proven end-to-end through the Custom GPT: `site` maps to `elementor_theme`; `standalone` maps to `elementor_canvas`.
- [x] Visual proof confirmed site mode restores Essentials header/footer and standalone removes theme header/footer without changing Elementor content.
- [x] Autonomous landing-page build proven with layout selection, multi-section Pixfort composition, offer-specific copy, and draft-only behavior.
- [x] Custom GPT schema expanded cleanly to 17 actions with no `$ref` use and explicit object properties for compatibility with the GPT Actions validator.
- [x] 0.5.2 adds authenticated image-only Media Library lookup at `GET /media/images`.
- [x] Filename lookup tolerates WordPress format conversion by matching the filename stem; original `.jpg` lookup resolved a stored `.avif` attachment.
- [x] Media lookup backend runtime proof: original filename `een-icoonontwerp-van-testpapier_362714-11527.jpg` resolved attachment `951899`, stored as AVIF.
- [x] Custom GPT `searchElementizeMediaImages` action proven through the public tunnel and returned attachment `951899` without the user providing an attachment ID.
- [x] Full natural-language media workflow proven on draft page 951865: GPT found attachment 951899, selected a writable media target, replaced the page image, created revision 951902, kept the page as draft, and the rendered page visually showed the replacement.

## Current environment

- WordPress 7.0.3 at `mijn-ibp.local`.
- Elementor 4.2.1 / Elementor Pro 4.2.1.
- Pixfort Core 4.1.3.
- Essentials 4.1.1.
- Installed runtime build: 0.5.2.
- Active development branch: `fastbuild/pixfort-library`.
- Current Custom GPT schema: 0.5.2.0 with 17 actions.
- Temporary public test tunnel: active Quick Tunnel; hostname is ephemeral and must not be treated as production configuration.

## Safety constraints

- Preserve Elementor structured data unless an operation deliberately changes it.
- Use WordPress/Elementor/Pixfort APIs rather than direct SQL.
- Structural and visual mutations require appropriate WordPress permission and stale-state protection.
- Mutations use fresh page content hashes/tokens where applicable and create pre-change revisions.
- `before`/`after` Pixfort insertion targets must be current top-level Elementor element IDs.
- Top-level removal accepts only current top-level Elementor `container` or legacy `section` IDs and verifies the ID is absent after save.
- New page creation is draft-only and cannot silently publish; failed Elementor initialization is rolled back.
- Visual probe is read-only, accepts at most four current catalogue section IDs, fetches only allowlisted Pixfort HTTPS thumbnail hosts, and caps each thumbnail response size.
- Visual writes remain restricted to explicitly supported targets; globals, dynamic values, inactive controls, and arbitrary Elementor internals are protected.
- Lifecycle writes are limited to Elementize-managed pages and require fresh lifecycle state plus explicit confirmation.
- Layout writes are limited to Elementize-managed drafts and require fresh layout state plus explicit confirmation.
- Media replacement requires a real editable WordPress image attachment; the GPT can now discover one through the guarded Media Library lookup instead of guessing an ID.
- Purchase keys, Application Passwords, cookies, nonces, HAR files, and proprietary theme/plugin source are never committed or returned by the API.
- Normal operation must not depend on browser automation.

## Important source/runtime findings

- Pixfort's AJAX endpoints are wrappers; Elementize can call the underlying PHP implementation directly.
- `Source_Pixfort::get_data()` replaces Elementor IDs, runs Elementor import processing, imports media/nested templates, and normalizes content against the target document.
- Essentials adds `pix_domain` + `purchase_key` request headers through `pixfort_el_remote_get_args`; Elementize reproduces this only internally during template download.
- `pixfort_elementor_library_data()` should not be called twice in one request in Essentials 4.1.1 because its `require_once('library.php')` can leave the second call without the local `$library` variable. The catalogue endpoint caches its first result; insertion intentionally lets `Source_Pixfort` make its single catalogue call.
- Custom GPT visual selection is proven against the real Pixfort catalogue, not merely metadata matching.
- Custom GPT autonomous draft creation + insertion, copy editing, visual editing, lifecycle control, layout control, and media discovery/replacement are now runtime-proven.
- WordPress may convert an uploaded source format (for example JPG) to another stored image format (observed AVIF); attachment discovery should not assume the uploaded extension survives.
- Pixfort `template_title` currently returns the slug for the tested insertion; this remains cosmetic cleanup.

## Current objective

Fix the important real-use quality gap exposed by broad rewrite testing: when the user asks to rewrite an entire page for a new business/topic, WP Builder can correctly update much of the page but may leave unrelated Pixfort/demo copy behind while reporting the rewrite as complete.

Use the smallest fix first: require a post-write verification read and second cleanup pass for broad copy transformations. Only add a new backend surface if the existing text read/write API cannot support that workflow reliably.

Acceptance condition: a natural-language request such as “edit all the copy so it becomes a grocery store” leaves no clearly unrelated source-topic or Pixfort demo copy in the editable text surface, and WP Builder does not claim completion until the verification read confirms the cleanup.

## Later

- Improve icon discovery beyond reusing already observed Pixfort icon values.
- Improve whole-page brand-palette verification without falsely claiming protected/global styles were changed.
- Decide whether page-title mutation should be exposed separately from Elementor visible-copy editing.
- Replace Quick Tunnel with stable public HTTPS staging/production connectivity.
