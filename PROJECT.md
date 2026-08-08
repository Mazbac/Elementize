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

## Fast V0.2 — Pixfort library

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
- [ ] Reopen Elementor and visually verify the two hero sections are ordered with `6bfbcd1a` before `4889ee30`.

## Current environment

- WordPress 7.0.3 at `mijn-ibp.local`.
- Elementor 4.2.1 / Elementor Pro 4.2.1.
- Pixfort Core 4.1.3.
- Essentials 4.1.1.
- Installed runtime build: 0.2.4.
- Candidate branch/build: `fastbuild/pixfort-library` / 0.2.4.

## Safety constraints

- Preserve Elementor structured data unless an operation deliberately changes it.
- Use WordPress/Elementor/Pixfort APIs rather than direct SQL.
- Mutating Pixfort operations require administrator permission and page edit permission.
- Mutations use a fresh page content hash and a pre-change revision.
- `before`/`after` Pixfort insertion targets must be current top-level Elementor element IDs.
- Purchase keys, Application Passwords, cookies, nonces, HAR files, and proprietary theme/plugin source are never committed or returned by the API.
- Normal operation must not depend on browser automation.

## Important source/runtime findings

- Pixfort's own AJAX endpoints are wrappers; Elementize can call the underlying PHP implementation directly.
- `Source_Pixfort::get_data()` replaces Elementor IDs, runs Elementor import processing, imports media/nested templates, and normalizes content against the target document.
- Essentials adds `pix_domain` + `purchase_key` request headers through `pixfort_el_remote_get_args`; Elementize reproduces this only internally during template download.
- `pixfort_elementor_library_data()` must not be called twice in one request in Essentials 4.1.1 because its `require_once('library.php')` leaves the second call without the local `$library` variable. The catalogue endpoint caches its first result; the insertion path intentionally lets `Source_Pixfort` make the single catalogue call itself.
- The first Pixfort runtime insert is proven visually and structurally; top-level inserted ID was `4889ee30` and pre-change revision was `951731`.
- 0.2.4 copy hardening is runtime-proven: `flex_justify_content` and `flex_justify_content_tablet` no longer appear, while the real AI hero copy still does.
- Controlled `before` insertion is runtime-proven at the API layer; visual ordering still needs confirmation.
- Pixfort `template_title` currently returns the slug for the tested insertion; this is cosmetic cleanup.

## Current objective

Visually confirm the `before` positioning result, then move to the next composition capability.

## Later

- Visual thumbnail delivery/matching for Custom GPT.
- New Elementor page creation from a section plan.
- Media/color/icon operations.
- Draft/delete/restore workflows.
- Public HTTPS staging/tunnel and Custom GPT OpenAPI schema.
