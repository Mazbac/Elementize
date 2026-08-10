# Elementize

Elementize is a small WordPress plugin for **guarded content editing on existing Elementor + Pixfort pages**.

It is deliberately not an autonomous page builder. AI can change recognized content while layout and visual design stay in human hands inside Elementor.

## What Elementize can edit

- Copy and text in recognized Elementor/Pixfort fields.
- Link and CTA destinations.
- Images using verified WordPress Media Library attachment IDs.
- Images attached to the current ChatGPT conversation, imported into the Media Library first.
- Public HTTPS images supplied by the user or found by ChatGPT, imported into the Media Library first.
- Pixfort icon values chosen from the installed Pixfort icon library.

## Media sources

Every page image replacement uses a WordPress attachment ID. Elementize supports three ways to obtain it:

1. **Media Library** — search existing WordPress image attachments.
2. **ChatGPT conversation** — import one attached conversation image from an allowed OpenAI file host.
3. **Public web URL** — import one direct public HTTPS image URL using WordPress safe-URL validation, bounded redirects, MIME verification, a 10 MiB limit, and a 60 MP limit. Source URL/page metadata is retained for provenance.

Elementize does not itself search the web. An AI client with browsing/search can find an image and pass the verified direct image URL to Elementize.

## Pixfort icons

Elementize exposes a read-only icon search endpoint that builds a bounded cached index from the installed Pixfort Core and active theme assets. It supports Pixfort **Line, Duotone, and Solid** values. Icon writes must use an exact value present in that installed-asset index; guessed icon IDs are rejected.

## What Elementize does not edit

- Page structure or Pixfort template insertion.
- Layout, spacing, sizing, responsive behavior, typography, colors, animation, or other design controls.
- Page publishing, trash/restore, or lifecycle state.
- Shared/global/embedded Elementor or Pixfort template documents.
- Dynamic Elementor values or global style references.
- Visual AI, Ollama, screenshots, aesthetic scoring, or automated repair.

## Safety model

Every content write is constrained by a fresh read of the current Elementor document. Elementize requires exact page identity/state, exact element/setting targets, expected current values, a successful pre-change WordPress revision, persisted `_elementor_data` verification, and verified rollback attempts when save verification fails. Published pages require an additional explicit confirmation.

## Requirements

- WordPress 6.5+
- PHP 8.0+
- Elementor
- Pixfort Core
- WordPress revisions enabled
- WordPress Application Passwords for Custom GPT access
- A public HTTPS address reachable by ChatGPT; local sites need a tunnel or staging URL

## Quick setup

1. Install and activate Elementize.
2. Open **WordPress Admin → Elementize**.
3. Resolve the readiness checks.
4. Open GPT Builder from the setup screen.
5. Copy the generated Instructions and Action schema.
6. Configure the single Action using **API Key → Basic** authentication.
7. Generate the one-time connection key in WordPress and add it to the Action.
8. Test with: `Call getElementizeStatus and show me the full raw result.`

The committed GPT schema contains exactly eight operations. The setup screen injects the site's public HTTPS origin at copy time.

## Runtime files

```text
elementize.php
includes/
  elementize-bootstrap.inc
  elementize-core.inc
  elementize-pixfort-icons.inc
  elementize-content.inc
  elementize-media-library.inc
  elementize-media-import.inc
  elementize-onboarding.inc
config/gpt/
  actions.openapi.yaml
  wp-builder-instructions.md
```

The small runtime surface is intentional. CI verifies the exact runtime set, eight-action contract, synchronized versions, schema parsing, and PHP syntax.
