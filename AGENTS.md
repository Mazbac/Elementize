# Elementize development rules

Elementize is intentionally a **direct field editor**, not a page builder or design agent.

## Product boundary

Allowed runtime capabilities:
- list existing Elementor pages;
- read existing editable fields;
- edit existing text;
- edit existing local colours;
- replace existing image/media fields with verified WordPress attachments;
- replace existing Pixfort icons with exact installed-library values;
- search WordPress images;
- import one image supplied in the CustomGPT conversation;
- provide simple CustomGPT onboarding and a stable public connection.

Everything else is out of scope unless the user explicitly changes the product direction again.

## Never reintroduce by default

Do not add Creative Control, template insertion, page structure writes, designer/blueprint/coherence/component-intelligence layers, rendered visual QA, autonomous design decisions, Activity/Undo UI, page lifecycle writes, global style mutation, Theme Builder writes, unrestricted Elementor JSON, or a React admin application.

Do not expose link editing unless the product scope is explicitly expanded; the current writable kinds are exactly `text`, `color`, `media`, and `pixfort_icon`.

## Mutation rules

Every page mutation must fail closed and use fresh exact state: page status/title, current content hash, exact element ID/path/current value, a pre-change WordPress revision, one Elementor save, persisted verification, and rollback on failed verification. Published pages require explicit live-page confirmation.

Dynamic values and global style references are read-only. Pixfort icon values must be verified against installed assets. Pixfort semantic colours must be validated against the exact installed widget control.

Never blindly retry after an unknown mutation result; read fresh state first.

## Connectivity

The preferred local-development connection is the stable free relay in `tools/`: one fixed Cloudflare `workers.dev` Worker fronts the ngrok account's fixed free development domain. The ngrok endpoint must restrict traffic to `/wp-json/elementize/v1/*`, and the Worker must enforce the same namespace. Windows restart handling restarts ngrok only; it must not recreate rotating Quick Tunnels or redeploy the Worker.

Keep Worker deploy source isolated from Wrangler/npm tooling and reuse healthy tooling. Preserve the existing `workers.dev` URL during relay migrations so CustomGPT does not need a new Action server URL.

Never commit Cloudflare credentials, ngrok authtokens, OAuth state, public tunnel runtime state, Application Passwords, connection keys, nonces, local runtime logs, `elementize-relay-runtime.json`, or `elementize-public-origin.txt`.

## Repository rules

- `main` is the accepted product state; feature work stays on its branch until explicitly merged.
- Keep the runtime and GPT Action surface minimal and documented.
- Keep normal public-site requests on the lightweight bootstrap; REST/editor modules must stay lazy-loaded.
- Keep plugin version and GPT Action schema version synchronized.
- Update GPT schema/instructions whenever the public REST contract changes.
- PHP syntax, the bare-essentials contract, and PowerShell parser checks must pass before completion.
- Prefer deleting dead architecture over leaving disabled parallel systems in the repository.
