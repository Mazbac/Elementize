# Rendered visual audit setup

Elementize 0.13.x can optionally capture a full-page screenshot of an Elementize-managed draft and ask a vision model for an advisory design critique.

The feature is opt-in. Normal completion audits do not call external rendering or AI services unless `include_visual=true` is requested.

## Provider

The initial provider is Cloudflare:

1. Browser Rendering / Browser Run captures the short-lived signed Elementize preview.
2. Workers AI analyzes the screenshot.
3. Elementize returns only bounded critique data and screenshot metadata. The screenshot is not persisted and the signed preview URL is not returned in the audit payload.

## Required Cloudflare configuration

Create a Cloudflare API token for the account that will run the audit. It needs the permissions required by Browser Rendering plus Workers AI. Use the least privilege available for the target account.

Add these constants to `wp-config.php` above the WordPress stop-editing line:

```php
define( 'ELEMENTIZE_CLOUDFLARE_ACCOUNT_ID', 'YOUR_ACCOUNT_ID' );
define( 'ELEMENTIZE_CLOUDFLARE_API_TOKEN', 'YOUR_API_TOKEN' );
```

Optional model override:

```php
define( 'ELEMENTIZE_VISUAL_AI_MODEL', '@cf/google/gemma-4-26b-a4b-it' );
```

Do not place these values in the Custom GPT Instructions, Actions schema, chat messages, source control, or normal WordPress content.

## Runtime check

After configuration, `getElementizeStatus` should report:

- `rendered_visual_audit: true`
- `rendered_visual_audit_configured: true`
- `rendered_visual_audit_provider: cloudflare`
- the configured model name

The API token itself is never returned.

## Acceptance test

Run `getElementizePageCompletionAudit` on an Elementize-managed draft with `include_visual=true`.

A successful result should include:

- `visual.available: true`
- `visual.screenshot.captured: true`
- screenshot byte count, dimensions, and SHA-256 only
- `visual.critique` with visual dimensions, findings, strengths, and priority actions
- no signed preview URL
- no screenshot bytes/base64

The first release is advisory-only. Visual findings do not become deterministic completion blockers by default.
