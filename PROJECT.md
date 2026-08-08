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

The smallest genuinely useful vertical slice is safe copy editing of an existing Elementor page through the Elementize API.

### Must work

- [x] Install and activate Elementize as a normal WordPress plugin.
- [x] Show Elementize directly in the WordPress admin sidebar with a basic environment/status screen.
- [ ] Expose authenticated REST endpoints suitable for a Custom GPT Action. Local Application Password authentication is proven; external GPT connection is not yet tested.
- [ ] List editable Elementor pages. Implemented; one real-site endpoint test remains.
- [x] Read an Elementor page's structured content / editable text inventory.
- [x] Update selected text values without rebuilding or flattening the Elementor layout.
- [x] Save through Elementor's document model rather than direct SQL.
- [x] Return a clear result that can be checked in Elementor.
- [ ] Reject unauthorized access and invalid/non-Elementor targets. Unauthenticated access returned 401; remaining invalid-target checks are not yet tested in the real site.
- [x] Create a pre-change revision before writes when WordPress revisions are enabled. Confirmed on 0.1.2 with numeric revision ID `951722`.

### Explicitly out of scope for V0.1

- Automatic screenshot-to-Pixfort visual matching.
- Full autonomous page reconstruction.
- General styling/media/icon editing.
- Production-grade multi-user OAuth.
- Destructive page deletion through the GPT.
- Broad support for arbitrary third-party Elementor addons beyond preserving their existing data.

## Current environment

- Platform / runtime: WordPress local development site (`mijn-ibp.local`).
- WordPress: 7.0.3.
- Elementor: 4.2.1.
- Elementor Pro: 4.2.1 observed in editor assets.
- Pixfort Core: 4.1.3.
- Theme: Essentials 4.1.1.
- Development/test environment: local WordPress installation; a public HTTPS endpoint will be needed before a Custom GPT Action can call the API remotely.

## Constraints

- Preserve Elementor's structured JSON and addon-specific settings unless a requested operation deliberately changes them.
- Prefer Elementor/WordPress APIs over direct database writes.
- Authentication is mandatory for every mutating or private endpoint.
- No secrets, cookies, WordPress nonces, Application Passwords, or HAR captures are stored in the repository.
- Mutations should be narrow, validated, and reversible where practical.
- The plugin should not depend on browser automation for normal operation.

## Current objective

- Perform one real-site read-only test of `GET /wp-json/elementize/v1/pages` so page discovery is proven, then move to the Pixfort template catalogue/insertion slice.

## Known blockers / issues

- `mijn-ibp.local` is not remotely reachable by a Custom GPT Action. Local API behavior can be built/tested first; GPT integration requires a public HTTPS test/staging endpoint or secure tunnel.

## Backlog

- [ ] Expose Pixfort remote template catalogue through Elementize.
- [ ] Fetch/import Pixfort sections/pages through the validated Pixfort mechanism.
- [ ] Return Pixfort thumbnail/preview metadata for template selection.
- [ ] Validate a reliable image-delivery path so the GPT can compare template previews visually.
- [ ] Create Elementor pages from a section plan.
- [ ] Add media, color, icon, and other targeted setting operations.
- [ ] Add safe draft/delete/restore workflows.
- [ ] Generate the Custom GPT OpenAPI schema and setup instructions.

## Working state

- Status: Elementize 0.1.2 is installed and active on the real local WordPress site. Authenticated targeted Elementor read/write and pre-change revision creation are proven.
- Real runtime checks passed: admin sidebar/status page, Elementor/Pixfort detection, Application Password authentication, unauthorized 401 behavior, read-only extraction of two test copy fields, targeted heading updates returning `saved: true`, stale-write 409 protection, visual Elementor verification after reopening, and numeric pre-change revision creation (`951722`).
- Visual verification confirmed the neighboring paragraph/layout remained intact and Elementor opened normally.
- Earlier fixture checks passed: recursive target traversal, nested setting-path update, blocked-field rejection, and missing-element behavior.
- Last known-good real environment checkpoint: 0.1.2 successfully changed `Changed by Elementize` to `Revision test passed` and returned `revision_id: 951722`.
- V0.1 usable: Core editing/safety path yes; page-discovery endpoint still needs one real-site check before closing the slice.

## Notes from real use

- A sanitized analysis of an Elementor/Pixfort HAR capture validated that Pixfort exposes machine-usable catalogue and template-fetch operations; details are recorded in `DISCOVERY.md`.
- Elementor source confirms its revision manager copies Elementor meta into WordPress revisions and restores that meta on revision restore.
- Real read/write testing used page-level content hashes plus expected old values. A deliberately stale/incorrect hash was rejected with HTTP 409 before mutation.
