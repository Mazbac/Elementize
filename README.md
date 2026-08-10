# Elementize

Elementize is a small WordPress plugin for **guarded content editing on existing Elementor + Pixfort pages**.

It is deliberately not an autonomous page builder. The AI can change page content while layout and visual design stay in human hands inside Elementor.

## What Elementize can edit

- Copy and text in recognized Elementor/Pixfort fields.
- Link and CTA destinations.
- Images, using verified WordPress Media Library attachment IDs.
- Pixfort icon values in recognized icon fields.
- Images attached to the current ChatGPT conversation can be imported into the Media Library first.

## What Elementize does not edit

- Page structure or Pixfort template insertion.
- Layout, spacing, sizing, responsive behavior, typography, colors, animation, or other design controls.
- Page publishing, trash/restore, or lifecycle state.
- Shared/global/embedded Elementor or Pixfort template documents.
- Dynamic Elementor values or global style references.
- Visual AI, Ollama, screenshots, aesthetic scoring, or automated repair.

## Safety model

Every page-content write is constrained by the current Elementor document. Elementize requires a fresh content hash, expected page status/title, exact element ID and setting path, and the expected current field value. It creates a WordPress revision before saving and verifies the requested changes after Elementor saves them. Published pages require an additional explicit confirmation.

## Requirements

- WordPress 6.5+
- PHP 8.0+
- Elementor
- Pixfort Core for the intended Pixfort workflow
- WordPress Application Passwords for Custom GPT access
- A public HTTPS address reachable by ChatGPT. Local sites need a tunnel or staging URL.

## Quick setup

1. Install and activate Elementize.
2. Open **WordPress Admin → Elementize**. The setup screen also opens automatically after activation.
3. Resolve the four readiness checks: Elementor, Pixfort Core, Application Passwords, and a public HTTPS API address.
4. Open GPT Builder from the setup screen.
5. Click **Copy Instructions** and paste them into the GPT Instructions field.
6. Click **Copy Action Schema** and paste it into one new GPT Action.
7. Set Action authentication to **API Key → Basic**.
8. Click **Generate Connection Key** in WordPress and paste the one-time key into the Action authentication field.
9. Test with: `Call getElementizeStatus and show me the full raw result.`

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

That small runtime surface is intentional.
