# Discovery

> Store durable technical findings here so AI does not have to rediscover the same system repeatedly.

## Goal being investigated

Determine whether Elementize can safely expose Elementor editing and the Essentials/Pixfort template library to a Custom GPT without browser automation.

## Environment

- Platform: local WordPress site at `mijn-ibp.local`.
- WordPress: 7.0.3.
- Elementor: 4.2.1.
- Elementor Pro: 4.2.1 observed in editor assets.
- Pixfort Core: 4.1.3.
- Theme: Essentials 4.1.1.

## Verified facts

Facts supported by authoritative documentation or source code.

- Elementor stores page configuration/content as structured JSON in WordPress post metadata. The page `content` is recursive and can contain nested containers and widgets.
- Elementor's documented JSON structure contains `title`, `type`, `version`, `page_settings`, and `content`; data structure version `0.4` is documented.
- Elementor's document model provides the current read/save path (`documents->get(...)->get_elements_data()` and document `save()`) rather than older DB editor helpers.
- Elementor's revision manager copies Elementor meta into WordPress revisions and restores that meta on revision restore.
- WordPress custom REST routes must be registered on `rest_api_init` and should use a `permission_callback`.
- Custom GPT Actions connect to an external API defined by an OpenAPI schema and support API-key or OAuth authentication.

## Observed behavior

### Pixfort catalogue

Observed in the supplied HAR capture. Sensitive values such as nonces/cookies are intentionally not recorded.

- Elementor/Pixfort sends a local WordPress AJAX request with action `pix_core_getElementorDemos` to `/wp-admin/admin-ajax.php`.
- The captured response contains 1,137 sections, 151 pages, 32 section categories, 8 page categories, and 1,439 library template records.
- Template metadata includes machine-usable fields such as `id`, `file`, `title`, `thumbnail`, `url`, `type`, `subtype`, `categories`, and in newer records `container_based`.
- Template thumbnails and preview URLs are supplied as normal web URLs.

### Pixfort template fetch

- Selecting a Pixfort section sends a local WordPress AJAX request with action `pix_core_getElementorTemplate`.
- Captured request fields included `action`, `nonce`, `editor_post_id`, `initial_document_id`, and `template_id`.
- Two captured template requests returned HTTP 200 Elementor JSON with `data.content`, `data.page_settings`, `data.version`, `data.title`, and `data.type`.
- Returned media URLs pointed to the local WordPress uploads directory and those local assets were immediately available afterward.

### Real Elementize REST read

- Elementize 0.1.1 authenticated successfully on the real local site using a WordPress Application Password.
- An unauthenticated request returned HTTP 401 from Elementize.
- `GET /wp-json/elementize/v1/pages/951706/text` returned the disposable Elementor test page and exposed exactly the expected copy fields:
  - Heading title: `Original heading`;
  - Text Editor content: `<p>Original paragraph</p>`.
- The response included a page-level `content_hash` used for optimistic concurrency control.

### Real Elementize REST write

- The first write attempt with malformed shell JSON was rejected by WordPress with `rest_invalid_json` / HTTP 400 before Elementize mutated anything.
- A corrected request using a JSON file succeeded against `POST /wp-json/elementize/v1/pages/951706/text`.
- The request targeted one Heading element and required both the previously read `content_hash` and `expected_value: Original heading`.
- The response returned:
  - `saved: true`;
  - `updated_count: 1`;
  - old value `Original heading`;
  - new value `Changed by Elementize`;
  - a new `content_hash`.
- The response also returned `revision_id: null`.
- Visual verification in the Elementor editor is still pending, so the API save is proven but preservation of the rendered/editor layout has not yet been visually confirmed.

## Inferences

- `pix_core_getElementorTemplate` appears to perform server-side asset importing/sideloading because its response already rewrites media to local upload URLs and those files are available immediately afterward. The exact internal Pixfort PHP implementation is not yet proven.
- The Pixfort library can be exposed to Elementize without screen scraping: the catalogue and template content are already machine-readable.
- Browser automation should not be needed for template discovery or template content retrieval.
- `revision_id: null` means Elementize's current explicit pre-save revision attempt did not return a revision ID in this real write. This does not prove that Elementor created no revision elsewhere during its save lifecycle, so actual revision state should be inspected before changing the implementation.

## Unknowns

Questions that still matter.

- Does the successfully saved test page reopen in Elementor with the paragraph/layout intact?
- Why did the explicit pre-save `wp_save_post_revision()` attempt return no revision ID, and did Elementor create a revision later in its own save lifecycle?
- What Pixfort PHP class/function is registered behind `wp_ajax_pix_core_getElementorDemos` and `wp_ajax_pix_core_getElementorTemplate`?
- What is the cleanest persistence path for inserting returned Pixfort content into an Elementor document outside the browser editor?
- What response format should Elementize use so a Custom GPT can genuinely inspect Pixfort thumbnails as images rather than merely receive thumbnail URLs?
- Which public HTTPS development/staging URL will be used when testing the Custom GPT Action? `mijn-ibp.local` itself is not remotely reachable.

## Evidence

### Evidence log

- Supplied HAR capture from the real local WordPress/Elementor/Pixfort environment. The HAR itself is intentionally not committed because it contains session-sensitive request data.
- Real local REST tests performed against Elementize 0.1.1 on the disposable `Elementize Test` page.
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

The original high-risk Pixfort discovery assumption is resolved.

The V0.1 save path now succeeds in the real runtime. The remaining validation risk is whether the targeted save preserves the Elementor editor/rendered structure exactly, plus whether rollback/revision behavior is actually available.

## Experiments / spikes

### Pixfort programmatic access experiment

**Question:** Can the real Essentials/Pixfort Elementor library be discovered and can a chosen section be retrieved as Elementor data without manually reconstructing it?

**Minimal test:** Capture browser network traffic while opening the Pixfort template library and selecting two sections.

**Result:** Pass.

**Conclusion:** Pixfort exposes a structured catalogue through `pix_core_getElementorDemos` and returns selected templates as Elementor JSON through `pix_core_getElementorTemplate`.

### Targeted Elementor copy-save experiment

**Question:** Can Elementize read a real Elementor page, update one recognized copy field through the document model, and receive a successful persisted result?

**Minimal test:** Create a disposable page with one normal Heading and one Text Editor, read both through Elementize, then update only the Heading title using the returned content hash and expected old value.

**Result:** API save pass; visual integrity verification pending.

**Conclusion:** Authentication, structured copy extraction, stale-write protection inputs, targeted mutation, and the Elementor document-save call all work end-to-end in the real local runtime. The response reported exactly one changed field. Explicit pre-change revision creation remains unresolved because `revision_id` was null.

## Chosen technical path

- Build Elementize as a WordPress plugin exposing its own authenticated REST API.
- Use WordPress/Elementor APIs for page/document operations; avoid direct SQL and avoid browser automation.
- For V0.1, implement one vertical slice: list Elementor pages -> read editable text -> update selected text -> save via Elementor -> verify in the real editor.
- Keep Pixfort catalogue/template insertion as the next slice after V0.1 is visually verified and the revision question is understood.

## Paths deliberately rejected

- Driving Elementor/Pixfort by simulated browser clicks as the primary integration mechanism.
- Giving the GPT unrestricted raw database or arbitrary Elementor JSON write access.
- Committing HAR captures, cookies, nonces, tokens, Application Passwords, or other session-sensitive evidence to Git.