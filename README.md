# Elementize

Elementize is a WordPress plugin that connects a **Custom GPT** to **Elementor** through **GPT Actions**.

The plugin exposes a small authenticated REST API instead of giving a GPT unrestricted access to the normal WordPress REST API.

## v0.1 scope

- Check WordPress + Elementor connection status.
- List/search Elementor pages.
- Read an Elementor page and its Elementor document data.
- Create a new Elementor page, defaulting to `draft`.
- Optionally allow direct publishing.
- Move an Elementor page to WordPress Trash.
- Generate a Bearer API key.
- Generate a site-specific OpenAPI 3.1 schema for a Custom GPT Action.
- Configure read/create/publish/trash permissions.
- Keep a small local activity log.

Permanent deletion is intentionally not included in v0.1.

## Requirements

- WordPress 6.5+
- PHP 8.0+
- Elementor
- Public HTTPS URL when testing from a Custom GPT Action

Elementor Pro and a specific WordPress theme are **not** required.

## Install for local development

1. Clone this repository into your WordPress plugins folder:

   ```bash
   cd wp-content/plugins
   git clone https://github.com/Mazbac/Elementize.git elementize
   ```

2. Activate **Elementor** and **Elementize** in WordPress.
3. Open **Elementize** in the WordPress admin menu.
4. Generate an API key and copy it immediately.
5. Configure the desired permissions.

The raw API key is returned to the browser only when it is generated. WordPress stores a keyed hash of the secret, not the recoverable API key.

## Custom GPT setup

In the GPT editor:

1. Create a new **Action**.
2. Set authentication to **API Key**.
3. Choose **Bearer**.
4. Paste the Elementize API key.
5. Copy the OpenAPI schema generated on the Elementize admin page and paste it into the Action schema editor.
6. In Preview, ask: `Check my WordPress connection.`

The generated schema uses your WordPress REST URL as its server URL.

### Local WordPress warning

A Custom GPT cannot call `localhost`, `.local`, or a private LAN address directly. The local WordPress site needs a publicly reachable HTTPS URL (for example a development tunnel) or a staging deployment before GPT Action requests can reach it.

## REST API

Namespace:

```text
/wp-json/elementize/v1/
```

Endpoints:

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/status` | Check connection and capabilities |
| `GET` | `/pages` | List/search Elementor pages |
| `GET` | `/pages/{id}` | Read an Elementor page |
| `POST` | `/pages` | Create an Elementor page |
| `DELETE` | `/pages/{id}` | Move an Elementor page to Trash |

All endpoints require:

```http
Authorization: Bearer elz_...
```

## Creating Elementor content

`POST /pages` accepts Elementor-style container/widget trees. Element IDs are optional; Elementize generates IDs when missing.

Example:

```json
{
  "title": "Example landing page",
  "status": "draft",
  "content": [
    {
      "elType": "container",
      "settings": {
        "content_width": "boxed"
      },
      "elements": [
        {
          "elType": "widget",
          "widgetType": "heading",
          "settings": {
            "title": "A new page from Elementize",
            "header_size": "h1"
          }
        },
        {
          "elType": "widget",
          "widgetType": "text-editor",
          "settings": {
            "editor": "<p>This page was created as an Elementor draft.</p>"
          }
        }
      ]
    }
  ]
}
```

Supported widgets in v0.1:

- `heading`
- `text-editor`
- `button`
- `image`
- `spacer`
- `divider`
- `icon`

Raw HTML widgets, shortcodes, arbitrary custom CSS, third-party widgets, media uploads, Elementor templates, and page updates are intentionally outside the first milestone.

## Security model

Elementize uses several layers:

1. A long random `elz_...` Bearer key.
2. Only a keyed hash is stored in WordPress.
3. The key is associated with the WordPress administrator who generated it.
4. Each request is checked against Elementize's per-operation permissions.
5. The associated WordPress user's capabilities are checked (`edit_pages`, `publish_pages`, `delete_pages`).
6. Page removal uses WordPress Trash rather than permanent deletion.
7. The API only exposes Elementor-page operations, not users, plugins, themes, settings, or arbitrary database access.

Regenerating the key immediately invalidates the previous key.

## Current limitations

This is the initial development release. It does not yet include:

- Updating an existing Elementor page.
- Duplicating an existing Elementor page/template.
- Media uploads.
- Yoast fields.
- Elementor Pro widgets.
- Theme/plugin-specific widgets such as pixfort/Essentials widgets.
- OAuth or multiple independent API keys.

Those should be added only after the base Action connection and Elementor document creation flow are tested end-to-end.

## License

GPL-2.0-or-later.
