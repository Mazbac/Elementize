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
- [x] Implement first guarded Pixfort insertion candidate in Elementize 0.2.2.
- [ ] Install 0.2.2 on the local site.
- [ ] Read the disposable Elementor test page to obtain a fresh content hash.
- [ ] Insert one selected Pixfort section at the end of that page.
- [ ] Confirm `saved: true`, numeric `revision_id`, and inserted top-level IDs.
- [ ] Reopen Elementor and visually verify the imported section/assets.

## Current environment

- WordPress 7.0.3 at `mijn-ibp.local`.
- Elementor 4.2.1 / Elementor Pro 4.2.1.
- Pixfort Core 4.1.3.
- Essentials 4.1.1.
- Installed runtime build: 0.2.1.
- Candidate branch/build: `fastbuild/pixfort-library` / 0.2.2.

## Safety constraints

- Preserve Elementor structured data unless an operation deliberately changes it.
- Use WordPress/Elementor/Pixfort APIs rather than direct SQL.
- Mutating Pixfort operations require administrator permission and page edit permission.
- Mutations use a fresh page content hash and a pre-change revision.
- Purchase keys, Application Passwords, cookies, nonces, HAR files, and proprietary theme/plugin source are never committed or returned by the API.
- Normal operation must not depend on browser automation.

## Important source findings

- Pixfort's own AJAX endpoints are wrappers; Elementize can call the underlying PHP implementation directly.
- `Source_Pixfort::get_data()` replaces Elementor IDs, runs Elementor import processing, imports media/nested templates, and normalizes content against the target document.
- Essentials adds `pix_domain` + `purchase_key` request headers through `pixfort_el_remote_get_args`; Elementize 0.2.2 reproduces this only internally during template download.
- `pixfort_elementor_library_data()` must not be called twice in one request in Essentials 4.1.1 because its `require_once('library.php')` leaves the second call without the local `$library` variable. The catalogue endpoint caches its first result; the insertion path intentionally lets `Source_Pixfort` make the single catalogue call itself.

## Current objective

Install/test 0.2.2 and prove one Pixfort section can be imported and appended safely to the disposable Elementor test page.

## Later

- Visual thumbnail delivery/matching for Custom GPT.
- Arbitrary insertion position and section ordering.
- New Elementor page creation from a section plan.
- Media/color/icon operations.
- Draft/delete/restore workflows.
- Public HTTPS staging/tunnel and Custom GPT OpenAPI schema.
