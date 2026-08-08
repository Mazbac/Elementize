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
- [x] Real-site catalogue test passed: `AI Agency Portfolio Intro` returned correctly with thumbnail, preview URL, category, subtype and `container_based` metadata.
- [x] Correct catalogue totals proven: 983 unique sections and 150 unique pages.
- [x] Fix Pixfort Core detector so Elementize branch names containing `pixfort` are not misidentified.
- [x] Fix 0.2.2 bootstrap guard regression; Elementize 0.2.3 restores admin menu + REST hook registration.
- [x] Install Elementize 0.2.3 on the local site.
- [x] Read disposable Elementor page 951706 and obtain fresh content hash.
- [x] Insert Pixfort section `ai-agency-portfolio-intro` at the end of the page through REST.
- [x] Runtime insert returned `saved: true`, revision `951731`, one top-level inserted element, ID `4889ee30`, and a new content hash.
- [x] Reopen Elementor and visually verify the imported section renders with styling/images and preserves existing content.
- [x] Fresh read after insertion proves top-level ID `4889ee30` contains the new AI hero widgets/copy and matches the post-insert content hash.
- [x] Implement Elementize 0.2.4 candidate: block layout/style keys such as `flex_justify_content` from the copy surface.
- [x] Implement guarded Pixfort insertion positions: `start`, `end`, `before`, `after`; before/after target only top-level Elementor IDs.
- [x] PHP lint passed for the exact committed 0.2.4 blob (`a6aa7124b74f9d0da65800b9286212c39e14aefe`).
- [x] Install 0.2.4 and verify layout settings disappear from `/pages/{id}/text` while real Pixfort copy remains.
- [x] Runtime-test controlled `before` insertion: inserted new top-level ID `6bfbcd1a` before anchor `4889ee30`, with revision `951734` and new content hash `baab2a4d4b1472b5ff15bfec535e4d668fd8ca65f84e66f295beebd4fbe72baa`.
- [x] Mark the newly inserted section unambiguously by changing nested heading widget `340abdf0` to `POSITION TEST — BEFORE`; targeted write saved with revision `951736` and content hash `30e52851e5777e61dd0e691ad73a7bc1fdd8a652438f1463dc7fdee7adc7ff80`.
- [x] Visual proof passed: Elementor shows `POSITION TEST — BEFORE` on the newly inserted hero directly above the untouched original `Transform Your Business With Strategic AI` hero.
- [x] Implement Elementize 0.2.5 candidate with guarded top-level section/container removal at `/pages/{id}/elements/remove`.
- [x] Removal candidate requires administrator + page edit permission, fresh content hash, exact top-level element ID, and pre-change revision; only `container`/legacy `section` targets are allowed.
- [x] Removal verifies after save that the requested top-level ID is actually gone and returns remaining top-level IDs plus the new content hash.
- [x] PHP lint passed for the exact committed 0.2.5 blob (`af2febe448a1c0056b3b6afe503a7765854d87c8`).
- [x] Install 0.2.5 on the local site.
- [x] Read page 951706 after the position-marker edit; fresh hash remained `30e52851e5777e61dd0e691ad73a7bc1fdd8a652438f1463dc7fdee7adc7ff80` and both heroes were present.
- [x] Remove disposable top-level section `6bfbcd1a` through the guarded endpoint.
- [x] Runtime removal returned `saved: true`, revision `951740`, removed type `container`, and remaining top-level IDs `4e0b32a`, `9f7d292`, `4889ee30`.
- [x] Original hero `4889ee30` remained present; removed ID `6bfbcd1a` is absent.
- [x] Post-removal content hash returned exactly to the pre-duplicate hash `8e487f247d1cc9893a7ec648baf0db94866a7baed9c9fc26159896c7bbc615fb`.
- [x] Visual removal proof passed: Elementor shows only the original `Transform Your Business With Strategic AI` hero and the `POSITION TEST — BEFORE` duplicate is gone.
- [x] Implement Elementize 0.2.6 candidate: `POST /pages/create` creates only a blank draft WordPress page and initializes it as an Elementor document.
- [x] 0.2.6 creation requires administrator + page creation permission, never accepts a publish status, and rolls the just-created draft back if Elementor initialization/verification fails.
- [x] PHP lint passed for the exact committed 0.2.6 blob (`1ec30ade0055ede34faecb13f0e0df9b90fb8dc5`).
- [x] Install 0.2.6 on the local site.
- [x] Create disposable draft page `Elementize Draft Build` through `/pages/create`: page ID `951743`, status `draft`, `is_elementor: true`, `top_level_count: 0`, slug `elementize-draft-build`, and usable Elementor edit URL.
- [x] New blank draft returned content hash `4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945`.
- [x] Insert real Pixfort section `ai-agency-portfolio-intro` into newly created draft page 951743 using that returned content hash.
- [x] Runtime composition returned `saved: true`, inserted top-level ID `2a8e1ffa`, revision `951745`, and new content hash `4825160dbff90dccca63881e5d13b0b26a6bd342d19f5b012d240525dee28637`.
- [ ] Reopen page 951743 in Elementor and visually verify the imported section renders correctly on a page created entirely through Elementize.

## Current environment

- WordPress 7.0.3 at `mijn-ibp.local`.
- Elementor 4.2.1 / Elementor Pro 4.2.1.
- Pixfort Core 4.1.3.
- Essentials 4.1.1.
- Installed runtime build: 0.2.6.
- Candidate branch/build: `fastbuild/pixfort-library` / 0.2.6.

## Safety constraints

- Preserve Elementor structured data unless an operation deliberately changes it.
- Use WordPress/Elementor/Pixfort APIs rather than direct SQL.
- Structural mutations require administrator permission plus page edit permission.
- Mutations use a fresh page content hash and a pre-change revision.
- `before`/`after` Pixfort insertion targets must be current top-level Elementor element IDs.
- Top-level removal accepts only current top-level Elementor `container` or legacy `section` IDs and verifies the ID is absent after save.
- New page creation defaults to draft and cannot silently publish; failed Elementor initialization is rolled back.
- Purchase keys, Application Passwords, cookies, nonces, HAR files, and proprietary theme/plugin source are never committed or returned by the API.
- Normal operation must not depend on browser automation.

## Important source/runtime findings

- Pixfort's own AJAX endpoints are wrappers; Elementize can call the underlying PHP implementation directly.
- `Source_Pixfort::get_data()` replaces Elementor IDs, runs Elementor import processing, imports media/nested templates, and normalizes content against the target document.
- Essentials adds `pix_domain` + `purchase_key` request headers through `pixfort_el_remote_get_args`; Elementize reproduces this only internally during template download.
- `pixfort_elementor_library_data()` must not be called twice in one request in Essentials 4.1.1 because its `require_once('library.php')` leaves the second call without the local `$library` variable. The catalogue endpoint caches its first result; the insertion path intentionally lets `Source_Pixfort` make the single catalogue call itself.
- The first Pixfort runtime insert is proven visually and structurally; top-level inserted ID was `4889ee30` and pre-change revision was `951731`.
- 0.2.4 copy hardening is runtime-proven: `flex_justify_content` and `flex_justify_content_tablet` no longer appear, while the real AI hero copy still does.
- Controlled `before` insertion is proven end-to-end, including visual order in Elementor.
- Guarded top-level removal is proven end-to-end; the page structure hash returned exactly to its pre-duplicate value and Elementor visually shows only the original hero.
- Safe blank draft page creation is runtime-proven in 0.2.6; new page 951743 was created as an empty Elementor draft through REST.
- Create → compose is runtime-proven at the API layer on page 951743: Elementize created the blank draft and then inserted a real Pixfort hero into it.
- Pixfort `template_title` currently returns the slug for the tested insertion; this is cosmetic cleanup.

## Current objective

Visually verify page 951743 in Elementor, then move from single-section composition to a multi-section draft-page build flow.

## Later

- Visual thumbnail delivery/matching for Custom GPT.
- Multi-section page creation from a section plan.
- Media/color/icon operations.
- Draft/delete/restore and explicit publish workflows.
- Public HTTPS staging/tunnel and Custom GPT OpenAPI schema.
