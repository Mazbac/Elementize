# Project

> This file is the concise source of truth for what this specific project is trying to achieve. Keep it current and short.

## Desired outcome

Elementize is a WordPress plugin that gives a Custom GPT a controlled API for working with WordPress and Elementor.

The intended product should eventually let the GPT:
- list, inspect, create, draft, update, and delete WordPress/Elementor pages;
- read Elementor page structure and edit copy, media, colors, icons, and supported settings;
- browse the Pixfort/Essentials template library, inspect available sections/pages and previews, and insert selected templates;
- use visual references supplied to the GPT to plan a page and choose suitable Pixfort sections.

## Target user

- A WordPress site owner using Elementor and the Essentials theme / Pixfort Core who wants to operate the site conversationally through a Custom GPT.

## Fast V0.1

The local authenticated Elementor copy-editing slice is complete and proven.

- [x] Install/activate Elementize and show it in the WordPress sidebar.
- [x] Authenticate with WordPress Application Passwords.
- [x] List editable pages and distinguish Elementor/non-Elementor pages.
- [x] Read editable Elementor copy fields.
- [x] Apply targeted copy updates through Elementor's document model.
- [x] Reject malformed/stale writes.
- [x] Preserve neighboring layout/editor state.
- [x] Create a real pre-change revision before writes when revisions are enabled.

## Fast V0.2 — Pixfort library

### Current slice

- [x] Inspect installed Pixfort Core 4.1.3 + Essentials 4.1.1 source and identify direct catalogue/template paths.
- [x] Implement a read-only Pixfort catalogue endpoint on `fastbuild/pixfort-library`.
- [x] Deduplicate catalogue records by template ID.
- [x] Return thumbnail + preview metadata, categories, subtype, and container-based flag.
- [x] Support section/page/all filtering, text search, category filtering, and pagination.
- [x] Pass local syntax/fixture tests against the supplied Essentials catalogue: 983 unique sections, 150 unique pages; AI Agency Portfolio Intro search resolves correctly.
- [ ] Install Elementize 0.2.0 on the real local WordPress site and validate the catalogue endpoint through REST.
- [ ] Add one controlled Pixfort section insertion endpoint and validate it on the disposable Elementor test page.

## Current environment

- Platform / runtime: WordPress local development site (`mijn-ibp.local`).
- WordPress: 7.0.3.
- Elementor: 4.2.1.
- Elementor Pro: 4.2.1 observed in editor assets.
- Pixfort Core: 4.1.3.
- Theme: Essentials 4.1.1.
- Installed proven build: Elementize 0.1.2.
- Candidate branch/build: `fastbuild/pixfort-library` / Elementize 0.2.0.

## Constraints

- Preserve Elementor's structured JSON and addon-specific settings unless a requested operation deliberately changes them.
- Prefer Elementor/WordPress/Pixfort APIs over direct database writes.
- Authentication is mandatory for private/mutating endpoints.
- No secrets, cookies, WordPress nonces, Application Passwords, HAR captures, purchase keys, or proprietary Pixfort/Essentials source are stored in the repository.
- Mutations should be narrow, validated, and reversible where practical.
- Do not use browser automation for normal operation.

## Current objective

- Install/test Elementize 0.2.0 on the real local site and prove `GET /wp-json/elementize/v1/pixfort/templates` returns the normalized Pixfort catalogue correctly.

## Known blockers / issues

- `mijn-ibp.local` is not remotely reachable by a Custom GPT Action. A public HTTPS test/staging endpoint or secure tunnel is still needed for the later GPT integration.
- Pixfort template downloads require the Essentials purchase-key/domain request context. The insertion slice must reproduce that context internally without exposing or logging the purchase key.

## Backlog

- [ ] Fetch/import one Pixfort section through `Source_Pixfort::get_data()` and save it into a disposable Elementor page.
- [ ] Validate a reliable image-delivery path so the GPT can compare Pixfort thumbnails visually.
- [ ] Create Elementor pages from a section plan.
- [ ] Add media, color, icon, and other targeted setting operations.
- [ ] Add safe draft/delete/restore workflows.
- [ ] Generate the Custom GPT OpenAPI schema and setup instructions.

## Working state

- V0.1 real runtime checks all passed, including numeric pre-change revision `951722`.
- V0.2.0 candidate adds the read-only `/pixfort/templates` endpoint and an admin status card for the Pixfort library.
- Local fixture checks against the supplied Essentials source passed for totals, search, and category filtering.
- A Pixfort loader quirk was found: `pixfort_elementor_library_data()` uses `require_once` and can return an undefined local `$library` if called twice in one PHP request. Elementize caches the first successful catalogue load per request to avoid that failure.
