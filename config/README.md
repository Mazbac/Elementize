# Elementize configuration

`gpt/` contains the two files used by the WordPress setup screen:

- `wp-builder-instructions.md` — Custom GPT operating instructions.
- `actions.openapi.yaml` — the small Custom GPT Action schema.

The setup screen replaces the placeholder server URL in the schema with the site's configured public HTTPS origin before copying it.
