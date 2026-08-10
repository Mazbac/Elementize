# Elementize

Elementize is a small WordPress plugin for **guarded content editing on existing Elementor + Pixfort pages**.

It is deliberately not an autonomous page builder. The AI can change recognized content while layout and visual design stay in human hands inside Elementor.

## What Elementize can edit

- Copy and text in recognized Elementor/Pixfort fields.
- Link and CTA destinations.
- Images, using verified WordPress Media Library attachment IDs.
- Pixfort icon values in recognized icon fields.
- One image attached to the current ChatGPT conversation can be imported into the Media Library first.

## What Elementize does not edit

- Page structure or Pixfort template insertion.
- Layout, spacing, sizing, responsive behavior, typography, colors, animation, or other design controls.
- Page publishing, trash/restore, or lifecycle state.
- Shared/global/embedded Elementor or Pixfort template documents.
- Dynamic Elementor values or global style references.
- Visual AI, Ollama, screenshots, aesthetic scoring, or automated repair.

## Safety model

Every content write is constrained by a fresh read of the current Elementor document. Elementize requires:

- exact page ID;
- expected page status and title;
- fresh Elementor content hash;
- exact element ID and setting path;
- expected current value or attachment ID;
- an enabled WordPress revision system and a successfully created pre-change revision;
- verification against the persisted `_elementor_data` after Elementor saves;
- a rollback attempt with rollback verification if the save cannot be verified;
- an additional explicit confirmation before changing an already-published page.

Duplicate writes to the same Elementor setting in one request are rejected. Dynamic/global values and shared template documents remain outside the writable surface.

## Requirements

- WordPress 6.5+
- PHP 8.0+
- Elementor
- Pixfort Core for the intended Pixfort workflow
- WordPress revisions enabled for guarded content writes
- WordPress Application Passwords for Custom GPT access
- A public HTTPS address reachable by ChatGPT. Local sites need a tunnel or staging URL.

## Quick setup

1. Install and activate Elementize.
2. Open **WordPress Admin → Elementize**. The setup screen also opens automatically after a normal single-plugin activation.
3. Resolve the readiness checks for Elementor, Pixfort Core, Application Passwords, and a public HTTPS API address.
4. Open GPT Builder from the setup screen.
5. Click **Copy Instructions** and paste them into the GPT Instructions field.
6. Click **Copy Action Schema** and paste it into one new GPT Action.
7. Set Action authentication to **API Key → Basic**.
8. Click **Generate Connection Key** in WordPress and paste the one-time key into the Action authentication field.
9. Test with: `Call getElementizeStatus and show me the full raw result.`

The committed GPT schema contains exactly six operations. The setup screen injects the site's public HTTPS origin into the schema at copy time; the repository keeps a neutral placeholder URL.

## Runtime files

```text
elementize.php
includes/
  elementize-bootstrap.inc
  elementize-core.inc
  elementize-content.inc
  elementize-media-library.inc
  elementize-chat-media.inc
  elementize-onboarding.inc
config/gpt/
  actions.openapi.yaml
  wp-builder-instructions.md
```

That small runtime surface is intentional. CI verifies the exact runtime file set, the six-action GPT contract, synchronized versions, schema parsing, and PHP syntax.
