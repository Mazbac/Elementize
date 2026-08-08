# Elementize V0.1.1

Elementize is a WordPress plugin candidate for controlled conversational access to WordPress and Elementor.

This branch is intentionally narrow. It proves the first useful path before broader page building and Pixfort automation are added.

## Current V0.1.1 capabilities

- Adds **Elementize** directly to the WordPress admin sidebar.
- Shows a status page for Elementize, Elementor, Pixfort Core, the active theme, and the REST endpoint.
- Lists editable WordPress pages and identifies which are Elementor documents.
- Reads a controlled inventory of recognized copy fields from an Elementor page.
- Updates selected copy fields without exposing arbitrary raw Elementor JSON writes.
- Rejects stale writes with a page `content_hash`.
- Requests a pre-change WordPress/Elementor revision before saving.
- Saves through Elementor's document model.

## REST endpoints

- `GET /wp-json/elementize/v1/status`
- `GET /wp-json/elementize/v1/pages`
- `GET /wp-json/elementize/v1/pages/{id}/text`
- `POST /wp-json/elementize/v1/pages/{id}/text`

All endpoints require an authenticated WordPress user with the relevant editing capability.

## Install / update on the local test site

1. Switch GitHub to branch `fastbuild/elementize-v0.1`.
2. Use **Code -> Download ZIP**.
3. In WordPress, go to **Plugins -> Add Plugin -> Upload Plugin**.
4. Upload the ZIP. If WordPress says the plugin already exists, replace the current plugin with the uploaded version.
5. Activate Elementize if needed.
6. Confirm **Elementize** appears in the WordPress left sidebar.
7. Open **Elementize** and confirm the status screen loads.

## First real validation

Do not merge this branch to `main` yet.

The next test is deliberately small:

1. Confirm the Elementize sidebar/status page works.
2. Choose a non-critical existing Elementor page.
3. Authenticate to the REST API locally.
4. Read the page text inventory.
5. Change one harmless heading or paragraph.
6. Open the page in Elementor and verify only that copy changed and the layout remained intact.
7. Confirm a revision exists and the page can be restored if necessary.

Only after that succeeds should the build move to Pixfort template catalogue and insertion endpoints.

## Pixfort discovery already established

A real Elementor/Pixfort HAR capture showed machine-usable Pixfort operations for the remote template catalogue and selected template JSON. Sensitive session data from that capture is not stored in this repository. Durable sanitized findings are in `DISCOVERY.md`.
