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
- [x] Install 0.2.8 on the local site.
- [x] Runtime no-slug regression test passed: `POST /pages/create` with only title created Elementor draft page 951758 with no PHP error, `top_level_count: 0`, and empty-document hash `4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945`.

## Current environment

- WordPress 7.0.3 at `mijn-ibp.local`.
- Elementor 4.2.1 / Elementor Pro 4.2.1.
- Pixfort Core 4.1.3.
- Essentials 4.1.1.
- Installed runtime build: 0.2.8.
- Active development branch: `fastbuild/pixfort-library`.
- 0.2.8 fix commit: `fde0979a39c12bc3373ad3e393cb5ebce023abb0`.
- Temporary public test tunnel: active Quick Tunnel; hostname is ephemeral and must not be treated as production configuration.

## Safety constraints

- Preserve Elementor structured data unless an operation deliberately changes it.
- Use WordPress/Elementor/Pixfort APIs rather than direct SQL.
- Structural mutations require administrator permission plus page edit permission.
- Mutations use a fresh page content hash and a pre-change revision.
- `before`/`after` Pixfort insertion targets must be current top-level Elementor element IDs.
- Top-level removal accepts only current top-level Elementor `container` or legacy `section` IDs and verifies the ID is absent after save.
- New page creation is draft-only and cannot silently publish; failed Elementor initialization is rolled back.
- Visual probe is read-only, accepts at most four current catalogue section IDs, fetches only allowlisted Pixfort HTTPS thumbnail hosts, and caps each thumbnail response size.
- Purchase keys, Application Passwords, cookies, nonces, HAR files, and proprietary theme/plugin source are never committed or returned by the API.
- Normal operation must not depend on browser automation.

## Important source/runtime findings

- Pixfort's AJAX endpoints are wrappers; Elementize can call the underlying PHP implementation directly.
- `Source_Pixfort::get_data()` replaces Elementor IDs, runs Elementor import processing, imports media/nested templates, and normalizes content against the target document.
- Essentials adds `pix_domain` + `purchase_key` request headers through `pixfort_el_remote_get_args`; Elementize reproduces this only internally during template download.
- `pixfort_elementor_library_data()` should not be called twice in one request in Essentials 4.1.1 because its `require_once('library.php')` can leave the second call without the local `$library` variable. The catalogue endpoint caches its first result; insertion intentionally lets `Source_Pixfort` make its single catalogue call.
- Custom GPT visual selection is now proven against the real Pixfort catalogue, not merely metadata matching.
- Custom GPT autonomous draft creation + insertion is proven on a disposable draft.
- Custom GPT controlled copy reading/writing is proven with stale-hash and expected-value safety available.
- Omitted draft slugs are now runtime-safe in 0.2.8. A newly created draft may keep `post_name` empty until WordPress assigns one later; this is acceptable and no longer crashes.
- Pixfort `template_title` currently returns the slug for the tested insertion; this is cosmetic cleanup.

## Current objective

Move beyond copy-only editing into controlled visual-property editing while preserving the same safety model. The next capability should inspect actual Elementor/Pixfort settings on the disposable autonomous draft and design a narrowly scoped read/write surface for media, colors, and icons rather than exposing arbitrary widget settings.

## Later

- Multi-section page creation from a visually selected section plan.
- Broader media/color/icon operations after narrow runtime proofs.
- Draft/delete/restore and explicit publish workflows.
- Replace Quick Tunnel with stable public HTTPS staging/production connectivity.
