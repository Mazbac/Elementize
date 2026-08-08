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

### Pixfort catalogue — HAR

Observed in the supplied HAR capture. Sensitive values such as nonces/cookies are intentionally not recorded.

- Elementor/Pixfort sends a local WordPress AJAX request with action `pix_core_getElementorDemos` to `/wp-admin/admin-ajax.php`.
- The captured response contains 1,137 sections, 151 pages, 32 section categories, 8 page categories, and 1,439 library template records.
- Template metadata includes machine-usable fields such as `id`, `file`, `title`, `thumbnail`, `url`, `type`, `subtype`, `categories`, and in newer records `container_based`.
- Template thumbnails and preview URLs are supplied as normal web URLs.

### Pixfort template fetch — HAR

- Selecting a Pixfort section sends a local WordPress AJAX request with action `pix_core_getElementorTemplate`.
- Captured request fields included `action`, `nonce`, `editor_post_id`, `initial_document_id`, and `template_id`.
- Two captured template requests returned HTTP 200 Elementor JSON with `data.content`, `data.page_settings`, `data.version`, `data.title`, and `data.type`.
- Returned media URLs pointed to the local WordPress uploads directory and those local assets were immediately available afterward.

### Pixfort source inspection — exact implementation

The user supplied the installed Pixfort Core 4.1.3 plugin and Essentials 4.1.1 theme source. The following was confirmed directly from those files; proprietary source itself is not committed to this repository.

- `pixfort-core/includes/options/core-options.php` registers:
  - `wp_ajax_pix_core_getElementorDemos` -> `CoreOptions::getElementorDemos()`;
  - `wp_ajax_pix_core_getElementorTemplate` -> `CoreOptions::getElementorTemplate()`.
- Both Pixfort AJAX handlers require a logged-in user, `manage_options`, and a valid Pixfort nonce. Elementize should preserve equivalent admin-level permission for Pixfort library operations instead of weakening the source plugin's permission model.
- `CoreOptions::getElementorDemos()` is only a thin wrapper. It calls `pixfort_elementor_library_data()` and returns that structured array.
- In Essentials, `inc/demo-content/elementor/loader.php` defines `pixfort_elementor_library_data()`. The function requires `library.php` and returns `$library`.
- Essentials loads that Elementor library loader only inside its `is_admin()` block. A normal REST request is not guaranteed to have that function loaded, so Elementize must explicitly require the active parent theme's `inc/demo-content/elementor/loader.php` when needed.
- Directly evaluating the supplied Essentials library source reproduced the HAR counts exactly: 1,137 sections, 151 pages, 32 section categories, 8 page categories, and 1,439 `library.templates` records.
- Example source records contain `id`, `file`, `title`, `thumbnail`, `url`, `type`, `subtype`, `categories`, and `container_based` where applicable. This is enough for a read-only searchable/paginated Elementize catalogue API without calling Pixfort AJAX at all.
- `CoreOptions::getElementorTemplate()` is also a wrapper. It requires `pixfort-core/includes/import/elementor/source.php`, creates `Elementor\TemplateLibrary\Source_Pixfort`, and calls `get_data([ 'template_id' => ..., 'editor_post_id' => ... ])`.
- `Source_Pixfort::get_data()` performs the key preparation work:
  - resolves/fetches the selected template;
  - replaces Elementor element IDs;
  - runs Elementor import processing via `process_export_import_content(..., 'on_import')`;
  - passes the imported content through the target document's `get_elements_raw_data(..., true)`.
- `Source_Pixfort::get_template_content()` resolves the selected template's `file` from the library and downloads JSON from the Pixfort import host. For Essentials the base is `https://import.pixfort.com/essentials/elementor/`.
- Nested Pixfort templates are handled inside that source path: referenced nested templates are imported locally and placeholder template IDs are replaced before the final content is returned.
- The source-level import processing explains the HAR observation that returned media already points at local WordPress uploads: this processing path is the correct one to reuse rather than reproducing media import logic in Elementize.
- Pixfort's shipped Elementor library JavaScript defines `pixInsertElementorContent`. It iterates over `e.content` and creates each top-level element in the current Elementor preview container (`document/elements/create` in the current editor path; legacy `addChildElement` otherwise). It does not manually reconstruct widget internals and does not use `page_settings` for normal insert behavior.
- Therefore the closest server-side equivalent for an Elementize insertion is: obtain prepared content through `Source_Pixfort::get_data()`, insert those returned top-level elements at a controlled top-level index in the existing Elementor document array, then save through the already-proven Elementor document model.

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

- Browser automation and loopback calls to `/wp-admin/admin-ajax.php` are unnecessary for the Pixfort integration. Elementize can call the underlying theme function/source class directly inside WordPress.
- The Pixfort source path should be treated as an integration dependency: Elementize should detect missing/changed functions/classes and fail clearly rather than silently falling back to raw database writes.
- The first Pixfort mutation test should insert one section into a disposable Elementor page at a known top-level position, using the same page-hash and pre-change-revision protections already proven in V0.1.

## Unknowns

- Does `Source_Pixfort::get_data()` work unchanged during an authenticated Elementize REST request when Elementize explicitly loads the Essentials library loader and Pixfort source file?
- Does merging its prepared `content` into the current top-level Elementor elements and calling `document->save()` render identically to Pixfort's editor-side insertion on the real site?
- What response format should Elementize use so a Custom GPT can genuinely inspect Pixfort thumbnails as images rather than merely receive thumbnail URLs?
- Which public HTTPS development/staging URL will be used when testing the Custom GPT Action? `mijn-ibp.local` itself is not remotely reachable.

## Evidence

- Supplied HAR capture from the real local WordPress/Elementor/Pixfort environment. The HAR itself is intentionally not committed because it contains session-sensitive request data.
- Supplied installed-source ZIPs for Pixfort Core 4.1.3 and Essentials 4.1.1. Their proprietary source is intentionally not committed.
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

The V0.1 Elementor read/write risk is resolved for the tested workflow.

The previous Pixfort architecture risk is also resolved at source level: the exact catalogue and template preparation functions/classes are now known. The active risk is narrower and testable: whether the prepared Pixfort content can be inserted and saved through Elementize in a REST request with the same visual result as the editor-side Pixfort insert.

## Experiments / spikes

### Pixfort programmatic access experiment

**Result:** Pass.

**Conclusion:** The HAR showed structured catalogue/template responses, and source inspection now proves the exact direct WordPress/PHP path behind them. Loopback AJAX/browser automation is not required.

### Targeted Elementor copy-save experiment

**Result:** Pass.

**Conclusion:** Authentication, page discovery, structured copy extraction, stale-write protection, targeted mutation, Elementor document saving, visual editor integrity, and pre-change revision creation work end-to-end for the tested Heading/Text Editor scenario.

## Chosen technical path

- Build Elementize as a WordPress plugin exposing its own authenticated REST API.
- Use WordPress/Elementor APIs for page/document operations; avoid direct SQL and avoid browser automation.
- Keep writes narrow and protect them with page hashes, expected old values, and pre-change revisions.
- V0.1 is complete for the local authenticated Elementor copy-editing slice.
- Pixfort slice:
  1. expose a read-only normalized catalogue directly from `pixfort_elementor_library_data()`;
  2. load `Source_Pixfort` directly for a selected template;
  3. insert returned top-level `content` into a disposable Elementor document;
  4. save through Elementor's document model and visually verify;
  5. only then generalize insertion positions/page creation and GPT-facing visual selection.

## Paths deliberately rejected

- Driving Elementor/Pixfort by simulated browser clicks as the primary integration mechanism.
- Looping Elementize back through Pixfort's own AJAX endpoints when the direct callable implementation is available in-process.
- Reimplementing Pixfort/Elementor media import logic instead of reusing `Source_Pixfort::get_data()`.
- Giving the GPT unrestricted raw database or arbitrary Elementor JSON write access.
- Committing HAR captures, proprietary Pixfort/Essentials source, cookies, nonces, tokens, Application Passwords, or other session-sensitive evidence to Git.
