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

The smallest useful vertical slice is safe copy editing of an existing Elementor page through the Elementize API.

### Must work

- [x] Install and activate Elementize as a normal WordPress plugin.
- [x] Show Elementize directly in the WordPress admin sidebar with a basic environment/status screen.
- [x] Expose authenticated REST endpoints suitable for external tooling. WordPress Application Password authentication is proven locally; remote Custom GPT reachability is a separate deployment step.
- [x] List editable Elementor pages. Real-site `/pages` test returned 23 pages, correctly distinguishing Elementor and non-Elementor pages.
- [x] Read an Elementor page's structured content / editable text inventory.
- [x] Update selected text values without rebuilding or flattening the Elementor layout.
- [x] Save through Elementor's document model rather than direct SQL.
- [x] Return a clear result that can be checked in Elementor.
- [x] Reject unsafe/stale writes. Unauthenticated access returned 401, malformed JSON was rejected before mutation, and a stale/incorrect content hash returned 409.
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

- Start the next Fast Build slice: expose the Pixfort/Essentials remote template catalogue through Elementize as read-only structured data, then prove one chosen template can be fetched and inserted safely into a disposable Elementor page.

## Known blockers / issues

- `mijn-ibp.local` is not remotely reachable by a Custom GPT Action. Local API behavior can continue to be built/tested first; GPT integration requires a public HTTPS test/staging endpoint or secure tunnel.
- Before productionizing Pixfort access, identify the Pixfort Core PHP handler behind the observed AJAX actions if practical; avoid depending on browser automation.

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

- Status: Fast V0.1 core is complete and proven on the real local WordPress/Elementor site.
- Installed build: Elementize 0.1.2.
- Real runtime checks passed: admin sidebar/status page, Elementor/Pixfort detection, Application Password authentication, page discovery, read-only copy extraction, targeted copy writes, malformed-request rejection, stale-write 409 protection, visual Elementor integrity after reopening, and numeric pre-change revision creation (`951722`).
- `/pages` real-site result: 23 editable pages across two result pages; `Elementize Test` returned first with `is_elementor: true`, while normal WordPress pages such as the default Sample Page were correctly returned with `is_elementor: false`.
- Last known-good checkpoint: 0.1.2 successfully changed `Changed by Elementize` to `Revision test passed`, returned `revision_id: 951722`, and page discovery succeeded afterward.
- V0.1 usable: Yes for the local authenticated Elementor copy-editing slice.

## Notes from real use

- A sanitized analysis of an Elementor/Pixfort HAR capture validated that Pixfort exposes machine-usable catalogue and template-fetch operations; details are recorded in `DISCOVERY.md`.
- Elementor source confirms its revision manager copies Elementor meta into WordPress revisions and restores that meta on revision restore.
- Real read/write testing used page-level content hashes plus expected old values. A deliberately stale/incorrect hash was rejected with HTTP 409 before mutation.
