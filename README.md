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

## Free persistent Cloudflare connection

A raw Cloudflare Quick Tunnel is free but receives a new `trycloudflare.com` hostname when it restarts. That makes it unsuitable as the permanent server URL in a CustomGPT Action.

Elementize therefore includes a free relay design for local Windows development:

1. A hidden `cloudflared` Quick Tunnel exposes the local WordPress site.
2. A tiny Cloudflare Worker on the account's free `workers.dev` subdomain proxies only `/wp-json/elementize/v1/*` to that current tunnel.
3. The Worker URL stays stable.
4. A Windows login monitor recreates the Quick Tunnel after reboot/crash and redeploys only the Worker's `TARGET_ORIGIN` value.
5. The stable Worker URL is written to `elementize-public-origin.txt`, which the plugin detects automatically.

This does not require a paid Cloudflare plan or a custom domain. Cloudflare account authorization and the first `workers.dev` deployment are one-time interactive steps. The account subdomain is derived from the Cloudflare account ID, while each Elementize site gets its own Worker name, local runtime directory, logs, mutex and Windows startup entry so multiple installations do not overwrite each other.

Run the command shown under **WordPress Admin → Elementize → Persistent connection**, or invoke `tools/windows/install-free-cloudflare-relay.ps1` directly.

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

There is no React frontend, template engine, designer agent, visual-QA runtime, Activity subsystem, or Creative Control runtime.

## Validation

`tests/bare-essentials-contract.php` locks the reduced runtime/API surface. GitHub CI runs the contract plus PHP syntax checks on PHP 8.0 and 8.3.

`elementize-public-origin.txt` is machine-local runtime state and must never be committed.
