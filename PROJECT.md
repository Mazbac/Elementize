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

- [x] Install and activate Elementize as a normal WordPress plugin. Confirmed on the real local site with 0.1.1.
- [x] Show Elementize directly in the WordPress admin sidebar with a basic environment/status screen. Confirmed on the real local site.
- [ ] Expose authenticated REST endpoints suitable for a Custom GPT Action. Local Application Password authentication is proven; external GPT connection is not yet tested.
- [ ] List editable Elementor pages. Implemented; real-site endpoint test pending.
- [x] Read an Elementor page's structured content / editable text inventory. Confirmed on a real Elementor test page.
- [ ] Update selected text values without rebuilding or flattening the Elementor layout. Implemented; real-site write test pending.
- [ ] Save through Elementor's document model rather than direct SQL. Implemented; real-site write test pending.
- [ ] Return a clear result that can be checked in Elementor. Implemented; real-site write test pending.
- [ ] Reject unauthorized access and invalid/non-Elementor targets. Unauthenticated request returned 401; remaining invalid-target checks are not yet tested in the real site.

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

- Perform one controlled copy write on the disposable Elementor test page, then reopen it in Elementor and verify the heading changed while the paragraph/layout remain intact.

## Known blockers

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

- Status: Elementize 0.1.1 is installed and active on the real local WordPress site.
- Plugin checkpoint: `elementize.php` commit `3e196b7abd61303360bd1ddc9a73076ad19fc8ee`.
- Real runtime checks passed: admin sidebar/status page, Elementor/Pixfort detection, Application Password authentication, unauthorized 401 behavior, and read-only extraction of the two test copy fields from a real Elementor page.
- Earlier fixture checks passed: recursive target traversal, nested setting-path update, blocked-field rejection, and missing-element behavior.
- Last known-good real environment checkpoint: test page successfully read through `/wp-json/elementize/v1/pages/{id}/text` before any API write.
- V0.1 usable: Not yet proven; one real write-and-verify test remains.

## Notes from real use

- A sanitized analysis of an Elementor/Pixfort HAR capture validated that Pixfort exposes machine-usable catalogue and template-fetch operations; details are recorded in `DISCOVERY.md`.
- Elementor source confirms its revision manager copies Elementor meta into WordPress revisions and restores that meta on revision restore.
- Real read test returned the normal Elementor Heading copy (`Original heading`) and Text Editor HTML (`<p>Original paragraph</p>`) as separate editable text items without exposing arbitrary page JSON for mutation.
