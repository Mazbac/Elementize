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

The preferred local-development connection is the free Cloudflare relay in `tools/`: a stable `workers.dev` Worker fronts a rotating Quick Tunnel and a hidden Windows monitor repairs the target automatically. Do not require a paid Cloudflare plan or purchased domain for Elementize setup.

Never commit Cloudflare credentials, OAuth state, tunnel URLs, Application Passwords, connection keys, nonces, local runtime logs, or `elementize-public-origin.txt`.

## Repository rules

- `main` is the accepted product state; feature work stays on its branch until explicitly merged.
- Keep the runtime and GPT Action surface minimal and documented.
- Keep plugin version and GPT Action schema version synchronized.
- Update GPT schema/instructions whenever the public REST contract changes.
- PHP syntax, the bare-essentials contract, and PowerShell parser checks must pass before completion.
- Prefer deleting dead architecture over leaving disabled parallel systems in the repository.
