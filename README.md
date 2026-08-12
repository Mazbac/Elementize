# Elementize

Elementize is a WordPress plugin for **guarded editing of existing Elementor + Pixfort pages through ChatGPT**.

Standard editing keeps structure and design fixed and focuses on safe content changes. Optional **Creative Control** expands that boundary for exactly one administrator-selected page at a time, allowing guarded structure and local design changes while preserving the same stale-state checks, revisions, persisted verification, Activity history, and Undo model.

## Editing modes

### Standard editing

Standard editing is the default. It can safely change recognized content on existing pages, including:

- Copy and text in recognized Elementor/Pixfort fields.
- Link and CTA destinations.
- Images using verified WordPress Media Library attachment IDs.
- Conversation/generated/public HTTPS images after importing them into the Media Library.
- Pixfort icon values chosen from the installed Pixfort icon library.

It does **not** change page structure or local design controls.

### Creative Control

An administrator can enable Creative Control for exactly one editable page. ChatGPT cannot enable, disable, or switch Creative Control itself.

On that selected page, Elementize can additionally:

- Search installed/local Pixfort templates.
- Inspect template structure, editable content, dependencies, and bounded design controls before insertion.
- Insert eligible template sections with fresh Elementor IDs.
- Remove elements.
- Duplicate elements.
- Move elements.
- Reorder exact child sets.
- Combine structure, content, icons, links, media, and design changes in one atomic Creative transaction.
- Change recognized local design controls such as color, spacing, radius, alignment, typography, and size when the exact current control is proven writable.
- Normalize supported Pixfort semantic color selectors, for example `gradient-primary` → `primary`, only when the installed exact Pixfort widget/control exposes that transition and the Pixfort theme option authoritatively resolves the theme role.
- Record each successful Creative transaction in Activity and restore the complete pre-change page state with guarded Undo.

Templates are treated as structural building blocks. The target page's existing design language remains the authority; Creative Control is not intended to assemble unrelated template styles into a collage.

## Media sources

Every page image replacement uses a WordPress attachment ID. Elementize supports four ways to obtain one:

1. **Media Library** — search existing WordPress image attachments.
2. **ChatGPT conversation** — import one attached conversation image from an allowed OpenAI file host.
3. **ChatGPT-generated image** — import the generated image through its conversation file reference or an allowed OpenAI-hosted URL.
4. **Public web URL** — import one direct public HTTPS image URL using WordPress safe-URL validation, bounded redirects, MIME verification, a 10 MiB limit, and a 60 MP limit. Source metadata is retained for provenance.

Elementize does not itself search the public web. A client with browsing/search can find a suitable image and pass the verified direct image URL to Elementize for import.

## Pixfort icons

Elementize exposes a read-only icon search built from the installed Pixfort Core and active theme assets. It supports Pixfort **Line, Duotone, and Solid** values. Icon writes must use an exact value returned by that installed-asset index; guessed icon IDs are rejected.

## Pixfort theme colors

Pixfort widgets often store semantic selectors such as `primary`, `secondary`, or `gradient-primary` rather than literal CSS colors.

Elementize does not treat those strings as arbitrary colors. A semantic theme-color write is allowed only when:

- the target is the exact installed Pixfort widget type;
- the exact setting is a supported selector control;
- the current selector is one of that control's real installed options;
- the requested transition is explicitly exposed as a normalization option; and
- the destination role is resolved by Pixfort's own theme option.

Elementor Kit tokens may corroborate the resolved value, but remain read-only and are never mutated as part of this process.

## Activity and guarded Undo

Successful verified Standard and Creative writes are recorded in **WordPress Admin → Elementize → Activity**.

For Creative transactions, Elementize also stores the exact pre-change Elementor snapshot and managed-root metadata on the pre-change revision so the whole transaction can be undone as one unit.

Undo is deliberately state-aware:

- the stored snapshot must still exist and pass its recorded hash check;
- the current page must still match the exact post-change hash from that Activity record;
- a new safety revision is created before Undo;
- the restored Elementor data is persisted and verified;
- Creative managed-root metadata is restored as well;
- published pages require an additional live-page confirmation.

If the page changed after an Activity record, Elementize refuses to restore that older state blindly.

Activity listing and guarded Undo are available both in the WordPress admin UI and through the Custom GPT Action surface.

## What Elementize deliberately does not edit

- Page lifecycle: creating, publishing, trashing, restoring, or changing status.
- Shared/global/embedded Elementor or Pixfort template documents.
- Elementor global style references.
- Dynamic Elementor values.
- Theme Builder documents such as site-wide headers and footers.
- Unrestricted Elementor JSON.
- Site-wide theme options or global design tokens through Creative Control.
- Arbitrary browser automation inside Elementor.

Creative Control is page-scoped and local by design.

## Verification model

Every mutation starts from a fresh read and requires exact guards for the current page state.

Creative transactions additionally require the current capability revision and explicit `confirm_creative_write=true`.

For a successful Creative save, Elementize verifies:

- the exact validated working tree reached Elementor's save input;
- Elementor's normalized save output was captured;
- persisted `_elementor_data` matches that normalized result;
- element IDs/order/widget types remain structurally identical to the validated transaction;
- every targeted content/design change persisted at its exact setting path;
- managed-root metadata persisted correctly.

If verification fails, Elementize attempts to restore the pre-change page.

`visual_render_verified=false` means exactly what it says: persisted Elementor data was verified, but Elementize must not claim that a rendered browser view was visually inspected unless a real visual pipeline provided that verification.

### Native rendered Visual QA

On the active Creative Control page, `getElementizePageVisualQA` captures a signed local Chromium render without exposing the preview URL. Capture is asynchronous, settles CSS animation/transition states, and trims tall screenshots to meaningful content bounds. With `analyze=true`, Elementize returns a ZIP conversation file through `openaiFileResponse`; the Custom GPT extracts `screenshot.png` and inspects it with native ChatGPT vision.

The server intentionally keeps `visual_analysis_verified=false` for native handoff. A GPT may only claim native visual verification after it actually inspected the returned PNG. Visual findings must be localized to fresh writable page-scoped controls before mutation. Design-profile controls disclose effective scope; a repair must not be narrower than the selected control effect. Shared/global/header/footer content may appear in the screenshot but remains outside page-scoped Creative Control.

## Requirements

- WordPress 6.5+
- PHP 8.0+
- Elementor
- Pixfort Core
- WordPress revisions enabled
- WordPress Application Passwords for Custom GPT access
- A public HTTPS address reachable by ChatGPT

Local WordPress sites can use a temporary Cloudflare Quick Tunnel.

## Setup

Open **WordPress Admin → Elementize → Setup**.

For first-time Custom GPT pairing, the Setup page keeps the required materials together:

- Custom GPT Instructions
- Action schema
- connection key generation
- connection test

For a Local site, Elementize also generates the exact `cloudflared` command. The command pins the Local hostname with `--http-host-header`, and adds `--no-tls-verify` when the Local origin uses HTTPS.

After a normal PC/tunnel restart the reconnect flow is intentionally short:

1. Start Cloudflare with the generated command.
2. Paste/save the new `https://…trycloudflare.com` URL.
3. Copy the refreshed Action schema into the existing GPT Action and run the test.

The existing API key and Custom GPT Instructions normally do not need to change after a tunnel restart, but the Instructions remain directly available in Setup.

## Main runtime files

```text
elementize.php
includes/
  elementize-bootstrap.inc
  elementize-core.inc
  elementize-pixfort-icons.inc
  elementize-pixfort-transport.inc
  elementize-pixfort-theme-tokens.inc
  elementize-content.inc
  elementize-design.inc
  elementize-tree.inc
  elementize-templates.inc
  elementize-creative.inc
  elementize-media-library.inc
  elementize-media-import.inc
  elementize-activity.inc
  elementize-connections.inc
  elementize-onboarding.inc
includes/runtime/
  elementize-template-response.inc
config/gpt/
  actions.openapi.yaml
  wp-builder-instructions.md
frontend/
  src/
tests/
  creative-control-contract.php
  natural-targeting-contract.php
```

CI covers the frontend build, the GPT/Elementize contract, and PHP syntax. The plugin header remains on the current product version while the Creative Control branch is being finalized.
