# Discovery

> Store durable technical findings here so AI does not have to rediscover the same system repeatedly.

## Goal being investigated

Determine whether Elementize can safely expose Elementor editing and the Essentials/Pixfort template library to a Custom GPT without browser automation.

## Environment

- Platform: local WordPress site at `mijn-ibp.local`.
- WordPress core assets observed with `ver=7.0.3`.
- Elementor editor assets observed with `ver=4.2.1`.
- Elementor Pro assets observed with `ver=4.2.1`.
- Pixfort Core assets primarily observed with `ver=4.1.3`.
- Theme/integration: Essentials + Pixfort Core + Elementor.
- Development/test context: Elementor editor opened on a new page while the Pixfort template library was browsed and two sections were selected/fetched.

## Verified facts

Facts supported by authoritative documentation or source code.

- Elementor stores page configuration/content as structured JSON in WordPress post metadata. The page `content` is recursive and can contain nested containers and widgets.
- Elementor's documented JSON structure contains `title`, `type`, `version`, `page_settings`, and `content`; data structure version `0.4` is documented.
- Elementor's official developer material identifies the document model as the replacement path for reading/saving editor data (`documents->get(...)->get_elements_data()` and document `save()`) rather than older DB editor helpers.
- WordPress custom REST routes must be registered on `rest_api_init` and should use a `permission_callback`.
- Custom GPT Actions connect to an external API defined by an OpenAPI schema and support API-key or OAuth authentication.

## Observed behavior

Directly observed in the supplied HAR capture. Sensitive values such as nonces/cookies are intentionally not recorded.

### Pixfort catalogue

- Elementor/Pixfort sends a local WordPress AJAX request with action `pix_core_getElementorDemos` to `/wp-admin/admin-ajax.php`.
- The captured response is JSON of roughly 1.49 MB and contains:
  - `demos.sections`: 1,137 entries;
  - `demos.pages`: 151 entries;
  - `demos.sectionsCategories`: 32 category records;
  - `demos.pagesCategories`: 8 category records;
  - `demos.library.templates`: 1,439 records across block/page/lp representations.
- Template metadata includes machine-usable fields such as `id`, `file`, `title`, `thumbnail`, `url`, `type`, `subtype`, `categories`, and in newer records `container_based`.
- Example template IDs seen in the capture include `ai-agency-portfolio-intro` and `ai-agency-pricing-intro-tables-clients-marquee`.
- Template thumbnails are public image URLs hosted primarily on `wordpress.assets.pixfort.com` and the legacy `pixfort-space.sfo2.cdn.digitaloceanspaces.com` host.
- When the template library was opened, the browser fetched 874 Pixfort thumbnail images in the capture.
- Template metadata also contains preview URLs on `essentials.pixfort.com/.../templates-library/?template=...`.

### Pixfort template fetch

- Selecting a Pixfort section sends a local WordPress AJAX request with action `pix_core_getElementorTemplate`.
- Captured request fields were:
  - `action`;
  - `nonce` (not recorded here);
  - `editor_post_id`;
  - `initial_document_id`;
  - `template_id`.
- Two captured requests used template IDs:
  - `ai-agency-portfolio-intro`;
  - `ai-agency-pricing-intro-tables-clients-marquee`.
- Both requests returned HTTP 200 JSON.
- Returned payload shape was `data.content`, `data.page_settings`, `data.version`, `data.title`, and `data.type`.
- Both captured payloads reported Elementor data version `0.4` and type `container`.
- The first returned about 19.8 KB of Elementor JSON; the second returned about 116 KB.
- Media URLs inside the returned Elementor data pointed to `mijn-ibp.local/wp-content/uploads/2026/08/...`, not to remote Pixfort demo media.
- The browser subsequently fetched those local uploaded assets successfully (200/304 responses were observed).

### Separate local template list

- A different action, `pix_get_templates_list`, returned locally available Pixfort/Elementor saved templates. This is not the remote Essentials section/page catalogue and should not be confused with `pix_core_getElementorDemos`.

### Save behavior

- No Elementor document-save/update request was captured after the two Pixfort template fetches.
- Therefore this HAR proves catalogue discovery and retrieval/import preparation, but does not by itself prove the exact persistence step used when an inserted section is finally saved to the page.

## Inferences

- `pix_core_getElementorTemplate` appears to perform server-side asset importing/sideloading because its response already rewrites media to local upload URLs and those files are available immediately afterward. The HAR does not expose the server-side PHP implementation, so the exact internal function is not yet proven.
- The Pixfort library can be exposed to Elementize without screen scraping: the catalogue and template content are already represented as structured machine-readable data.
- Browser automation should not be needed for template discovery or template content retrieval.

## Unknowns

Questions that still matter.

- What Pixfort PHP class/function is registered behind `wp_ajax_pix_core_getElementorDemos` and `wp_ajax_pix_core_getElementorTemplate`? Inspecting Pixfort Core PHP source would let Elementize call the underlying implementation directly rather than loop back through `admin-ajax.php`.
- What is the cleanest persistence path for inserting returned Pixfort content into an Elementor document outside the browser editor?
- Does the exact Elementor 4.2.1 document `save()` behavior require any additional data normalization for third-party Pixfort widgets/containers?
- What response format should Elementize use so a Custom GPT can genuinely inspect Pixfort thumbnails as images rather than merely receive thumbnail URLs?
- Which public HTTPS development/staging URL will be used when testing the Custom GPT Action? `mijn-ibp.local` itself is not remotely reachable.

## Evidence

### Evidence log

- Supplied HAR capture from the real local WordPress/Elementor/Pixfort environment. The HAR itself is intentionally not committed because it contains session-sensitive request data.
- Elementor developer documentation:
  - https://developers.elementor.com/docs/data-structure/index.html
  - https://developers.elementor.com/docs/data-structure/general-structure/
  - https://developers.elementor.com/docs/data-structure/page-content/
- Elementor official developer deprecation reference documenting the document read/save replacements:
  - https://developers.elementor.com/v2-6-0-planned-deprecations/
- WordPress REST endpoint documentation:
  - https://developer.wordpress.org/reference/functions/register_rest_route/
  - https://developer.wordpress.org/rest-api/extending-the-rest-api/adding-custom-endpoints/
- OpenAI Custom GPT Actions configuration:
  - https://help.openai.com/en/articles/9442513

## Critical risk / assumption

The original high-risk assumption was whether Pixfort templates could be discovered and fetched programmatically. The HAR validates that mechanism.

The main V0.1 implementation risk is now narrower: save targeted Elementor text changes through the Elementor document model without corrupting or flattening existing third-party Elementor/Pixfort data.

## Experiments / spikes

### Pixfort programme access experiment

**Question:**  
Can the real Essentials/Pixfort Elementor library be discovered and can a chosen section be retrieved as Elementor data without manually reconstructing it?

**Minimal test:**  
Capture browser network traffic while opening the Pixfort template library and selecting two sections.

**Result:**  
Pass.

**Conclusion:**  
Pixfort exposes a structured catalogue through `pix_core_getElementorDemos` and returns selected templates as Elementor JSON through `pix_core_getElementorTemplate`. The mechanism is suitable for an Elementize integration, subject to identifying the clean server-side callable path before productionizing it.

## Chosen technical path

- Build Elementize as a WordPress plugin exposing its own authenticated REST API.
- Use WordPress/Elementor APIs for page/document operations; avoid direct SQL and avoid browser automation.
- For V0.1, implement one vertical slice: list Elementor pages -> read editable text -> update selected text -> save via Elementor -> verify in the real editor.
- Keep Pixfort catalogue/template insertion as the next slice. The discovery risk is resolved, but the direct PHP integration path should be inspected before implementation.

## Paths deliberately rejected

- Driving Elementor/Pixfort by simulated browser clicks as the primary integration mechanism.
- Giving the GPT unrestricted raw database or arbitrary Elementor JSON write access.
- Committing HAR captures, cookies, nonces, tokens, or other session-sensitive evidence to Git.
