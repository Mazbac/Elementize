# Elementize

Elementize is a deliberately small WordPress bridge between a CustomGPT and **existing Elementor + Pixfort page content**.

It does four things:

1. Edit existing text.
2. Edit existing local colours.
3. Replace existing image/media fields.
4. Replace existing Pixfort icons.

That is the complete product scope.

## Explicitly not supported

Elementize does not create pages, insert templates, move/reorder/duplicate/delete elements, change layout or spacing, run design intelligence, perform rendered visual QA, manage responsive composition, publish pages, or mutate shared/global/header/footer content.

The user builds the page in Elementor/Pixfort. Elementize only edits fields already present on that page.

## Direct media from CustomGPT

A user can attach an image directly in the CustomGPT conversation. `importElementizeConversationImage` verifies and imports that image into the WordPress Media Library and returns the attachment ID. The same GPT can immediately use that ID to replace an existing page image field.

## Safety model

Every page write requires fresh page identity, page status, page title, a fresh content hash, the exact current field value, a pre-change WordPress revision, Elementor persistence, and post-save verification. Published pages require explicit live-page confirmation.

Global Elementor style references and dynamic values remain read-only. Pixfort semantic colour selectors are changed only when the installed Pixfort widget exposes the exact destination as a real allowed option. Pixfort icon IDs must come from the installed icon library.

## CustomGPT Action surface

The public Action contract has only seven operations:

- `getElementizeStatus`
- `listElementizePages`
- `getElementizePageContent`
- `updateElementizePageContent`
- `searchElementizeMediaImages`
- `importElementizeConversationImage`
- `searchElementizePixfortIcons`

The Action schema and GPT instructions live in `config/gpt/` and are copied from the single WordPress **Elementize** admin screen.

## Free stable local connection

Elementize uses two fixed public addresses for local Windows development instead of a rotating Quick Tunnel:

1. The free ngrok account's automatically assigned development domain exposes the local WordPress site.
2. An ngrok Traffic Policy rejects every path except `/wp-json/elementize/v1/*` before traffic reaches WordPress.
3. A tiny Cloudflare Worker on the free `workers.dev` subdomain proxies only the Elementize REST namespace to that ngrok domain.
4. The `workers.dev` URL remains the permanent CustomGPT Action server URL.
5. Windows startup only restarts the same ngrok development endpoint; it does not create a new hostname or redeploy the Worker.

The one-time setup requires a free ngrok account/authtoken and a free Cloudflare account. No custom domain is required. WordPress Application Password authentication still protects the Elementize REST API.

A free ngrok account has one automatically assigned development domain. Elementize therefore treats one free ngrok account as one concurrently active local site; use a separate ngrok account if multiple local Elementize sites must be online at the same time.

Each Elementize site keeps its runtime, settings, logs, Worker source and Wrangler tooling in its own directory under `%LOCALAPPDATA%\Elementize\sites`. The ngrok authtoken is stored only in that machine-local runtime and must never be committed to the repository.

Run the command shown under **WordPress Admin → Elementize → Persistent connection**, or invoke `tools/windows/install-stable-relay.ps1` directly. After setup, the same admin screen can start, stop, fully disable or re-enable the relay and Windows autostart.

## Runtime files

```text
elementize.php
includes/
  elementize-bootstrap.inc
  elementize-core.inc
  elementize-content.inc
  elementize-pixfort-theme-tokens.inc
  elementize-pixfort-icons.inc
  elementize-media-library.inc
  elementize-media-import.inc
  elementize-onboarding.inc
config/gpt/
tools/cloudflare-relay/
tools/windows/
tests/bare-essentials-contract.php
```

There is no React frontend, template engine, designer agent, visual-QA runtime, Activity subsystem, or Creative Control runtime. Normal public-site requests load only the lightweight Elementize bootstrap; editor/media/Pixfort modules are lazy-loaded for REST calls and setup code is loaded only in wp-admin.

## Validation

`tests/bare-essentials-contract.php` locks the reduced runtime/API surface and the relay/runtime separation. GitHub CI runs PHP syntax + the contract on PHP 8.0 and 8.3, while OpenAPI, Worker and PowerShell checks run once per workflow.

`elementize-public-origin.txt` is machine-local runtime state and must never be committed.
