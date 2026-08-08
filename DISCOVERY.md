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

- Elementor stores page configuration/content as structured JSON in WordPress post metadata. The page `content` is recursive and can contain nested containers and widgets.
- Elementor's document model provides the current read/save path (`documents->get(...)->get_elements_data()` and document `save()`) rather than older DB editor helpers.
- Elementor's revision manager copies Elementor meta into WordPress revisions and restores that meta on revision restore.
- WordPress `wp_save_post_revision()` normally skips a new revision when its revisioned post fields are unchanged. The `wp_save_post_revision_check_for_changes` filter can override that comparison.
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

### Real Elementize V0.1 runtime

- Elementize authenticated successfully on the real local site using a WordPress Application Password.
- An unauthenticated request returned HTTP 401 from Elementize.
- `GET /wp-json/elementize/v1/pages/951706/text` returned the expected Heading and Text Editor copy fields.
- A malformed JSON write was rejected by WordPress before mutation.
- A valid targeted write succeeded with `saved: true` and exactly one changed field.
- Reopening the page in Elementor confirmed the target heading changed while the neighboring paragraph and layout remained intact.
- An incorrect/stale page hash was rejected with HTTP 409 `elementize_content_changed` before mutation.
- Elementize 0.1.2 created a real pre-change revision before a write; the successful retest returned numeric `revision_id: 951722`.
- `GET /wp-json/elementize/v1/pages?per_page=20` succeeded on the real site and reported 23 editable pages across two result pages.
- The page-discovery result correctly marked `Elementize Test` as `is_elementor: true` and ordinary WordPress pages such as the default Sample Page as `is_elementor: false`.

## Inferences

- `pix_core_getElementorTemplate` appears to perform server-side asset importing/sideloading because its response already rewrites media to local upload URLs and those files are available immediately afterward. The exact internal Pixfort PHP implementation is not yet proven.
- The Pixfort library can be exposed to Elementize without screen scraping: the catalogue and template content are already machine-readable.
- Browser automation should not be needed for template discovery or template content retrieval.

## Unknowns

- What Pixfort PHP class/function is registered behind `wp_ajax_pix_core_getElementorDemos` and `wp_ajax_pix_core_getElementorTemplate`?
- What is the cleanest persistence path for inserting returned Pixfort content into an Elementor document outside the browser editor?
- What response format should Elementize use so a Custom GPT can genuinely inspect Pixfort thumbnails as images rather than merely receive thumbnail URLs?
- Which public HTTPS development/staging URL will be used when testing the Custom GPT Action? `mijn-ibp.local` itself is not remotely reachable.

## Evidence

- Supplied HAR capture from the real local WordPress/Elementor/Pixfort environment. The HAR itself is intentionally not committed because it contains session-sensitive request data.
- Real local REST and visual-editor tests performed against Elementize 0.1.1 and 0.1.2 on the disposable `Elementize Test` page.
- Elementor developer documentation:
  - https://developers.elementor.com/docs/data-structure/index.html
  - https://developers.elementor.com/docs/data-structure/general-structure/
  - https://developers.elementor.com/docs/data-structure/page-content/
- Elementor official developer deprecation reference:
  - https://developers.elementor.com/v2-6-0-planned-deprecations/
- WordPress REST endpoint documentation:
  - https://developer.wordpress.org/reference/functions/register_rest_route/
  - https://developer.wordpress.org/rest-api/extending-the-rest-api/adding-custom-endpoints/
- WordPress revision references:
  - https://developer.wordpress.org/reference/functions/wp_save_post_revision/
  - https://developer.wordpress.org/reference/hooks/wp_save_post_revision_check_for_changes/
- OpenAI Custom GPT Actions configuration:
  - https://help.openai.com/en/articles/9442513

## Critical risk / assumption

The V0.1 Elementor read/write risk is resolved for the tested workflow: page discovery, copy extraction, targeted mutation, stale-write protection, visual editor integrity, and pre-change revision creation all work in the real runtime.

The active high-risk assumption is now narrower: whether Pixfort's observed template catalogue/fetch path can be wrapped cleanly inside Elementize and persisted into Elementor without depending on browser-only state or brittle loopback AJAX.

## Experiments / spikes

### Pixfort programmatic access experiment

**Result:** Pass.

**Conclusion:** Pixfort exposes a structured catalogue through `pix_core_getElementorDemos` and returns selected templates as Elementor JSON through `pix_core_getElementorTemplate`.

### Targeted Elementor copy-save experiment

**Result:** Pass.

**Conclusion:** Authentication, page discovery, structured copy extraction, stale-write protection, targeted mutation, Elementor document saving, visual editor integrity, and pre-change revision creation work end-to-end for the tested Heading/Text Editor scenario.

## Chosen technical path

- Build Elementize as a WordPress plugin exposing its own authenticated REST API.
- Use WordPress/Elementor APIs for page/document operations; avoid direct SQL and avoid browser automation.
- Keep writes narrow and protect them with page hashes, expected old values, and pre-change revisions.
- V0.1 is complete for the local authenticated Elementor copy-editing slice.
- Next slice: expose Pixfort catalogue data read-only first; then fetch/import one selected template into a disposable Elementor page and visually verify it.

## Paths deliberately rejected

- Driving Elementor/Pixfort by simulated browser clicks as the primary integration mechanism.
- Giving the GPT unrestricted raw database or arbitrary Elementor JSON write access.
- Committing HAR captures, cookies, nonces, tokens, Application Passwords, or other session-sensitive evidence to Git.
