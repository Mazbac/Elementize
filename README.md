# Elementize

Elementize is a WordPress plugin that exposes a controlled API for working with Elementor from an external AI client such as a Custom GPT.

This repository also contains the Fast Build OS project/discovery files used to keep implementation evidence-driven and scoped.

## Current V0.1

The first vertical slice is deliberately limited to safe copy editing of existing Elementor pages.

Implemented endpoints:

- `GET /wp-json/elementize/v1/status`
- `GET /wp-json/elementize/v1/pages`
- `GET /wp-json/elementize/v1/pages/{id}/text`
- `POST|PUT|PATCH /wp-json/elementize/v1/pages/{id}/text`

V0.1 does **not** expose arbitrary Elementor JSON writes, styling/media changes, page deletion, or Pixfort insertion yet.

## Safety model

- Every endpoint requires an authenticated WordPress user with page-editing capability.
- Per-page operations also require `edit_post` permission for that page.
- Text updates are limited to recognized copy settings; URL/media/style/query-like settings are rejected.
- A `content_hash` returned by the read endpoint must still match before a write is accepted, preventing stale edits from overwriting newer page changes.
- A WordPress/Elementor revision is requested before the Elementor document is saved.
- Elementor's document model is used for persistence rather than direct database writes.
- The API never needs browser cookies or Elementor editor nonces from a Custom GPT.

## Local test

1. Check out the `fastbuild/elementize-v0.1` branch in the WordPress plugins directory, or install a ZIP of that branch.
2. Activate **Elementize** in WordPress.
3. Create a dedicated WordPress Application Password for the test user rather than sharing the user's normal WordPress password.
4. Call the status endpoint with HTTP Basic authentication over HTTPS.
5. List pages, read one Elementor page's text inventory, then make one small copy change on a non-critical test page.
6. Open the page in Elementor and verify that the text changed while the layout remained intact.
7. If the test passes, continue with the Pixfort catalogue/template-insertion slice recorded in `DISCOVERY.md`.

Example request shape for a text update:

```json
{
  "content_hash": "HASH_FROM_GET_TEXT",
  "updates": [
    {
      "element_id": "ELEMENT_ID_FROM_GET_TEXT",
      "setting_path": ["title"],
      "expected_value": "Old heading",
      "value": "New heading"
    }
  ]
}
```

## Current evidence

The supplied Elementor/Pixfort network capture established that the Essentials/Pixfort library is machine-readable:

- `pix_core_getElementorDemos` returns the remote section/page catalogue and preview metadata.
- `pix_core_getElementorTemplate` returns a selected template as Elementor JSON.
- Captured returned templates already referenced media imported into the local WordPress uploads directory.

The sensitive HAR capture itself is intentionally not stored in Git. Sanitized findings are recorded in `DISCOVERY.md`.

## Project files

- `elementize.php` — current V0.1 plugin implementation.
- `PROJECT.md` — product outcome, V0.1 scope, backlog, and working state.
- `DISCOVERY.md` — validated technical findings and remaining unknowns.
- `FAST_BUILD_OS.md` — development process and stop-loss rules.
- `AGENTS.md` — operating instructions for coding agents.

## Status

The V0.1 code is syntax-checked and its core recursive text-targeting helper has been exercised with local fixture tests. It has **not yet been validated inside the real WordPress/Elementor installation**, so it should be treated as a test candidate, not a known-good release.
