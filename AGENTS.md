# Elementize development rules

Elementize has one narrow product scope: guarded content editing for existing Elementor + Pixfort pages.

## In scope

- Read existing Elementor pages.
- Resolve natural-language references to exact safe Elementor targets.
- Use screenshot clues interpreted by ChatGPT to locate existing page content; Elementize itself does not capture or visually analyze screenshots.
- Reuse prior selection context and recognize repeated sibling/card groups.
- Read/write recognized text and link fields.
- Swap recognized image fields with verified WordPress image attachments.
- Search the Media Library.
- Import one current-conversation image from an allowed OpenAI file host.
- Import one ChatGPT-generated image through the dedicated generated-image handoff.
- Import one direct public HTTPS image with safe URL/MIME/size/dimension checks and provenance metadata.
- Search the installed Pixfort icon library and write exact verified Line, Duotone, or Solid values.
- Setup/onboarding for a Custom GPT.
- Stale-state guards, mandatory revisions, persisted save verification, and verified rollback attempts.
- Retry-safe behavior where possible, including media-import deduplication and bounded recovery instructions for the Custom GPT.

## Out of scope

Do not add page generation, template selection/insertion, section removal, page lifecycle controls, design settings, color/typography/spacing controls, browser automation, plugin-internal screenshot capture/vision, Ollama, aesthetic scoring, visual QA, repair loops, autonomous design ranking, or autonomous layout/design behavior unless the product owner explicitly changes scope.

## Mutation rules

Never expose unrestricted Elementor JSON writes. A mutation must use a fresh page read, exact page identity/state, exact element ID and setting path, expected current value/attachment, a successful pre-change revision, persisted `_elementor_data` verification, and verified rollback attempts. Reject duplicate targets, dynamic/global values, and shared template writes.

Never blindly repeat a mutation after an unknown result. Fresh-read first, recognize already-persisted desired state, and rebuild from fresh state before any bounded retry. Multi-batch edits must get a fresh hash after each successful batch.

Pixfort icon mutations must use values verified against the installed Pixfort icon index. Remote image imports must be public HTTPS only, use WordPress safe HTTP validation, and remain bounded by file-size/dimension limits. Media imports should be idempotent/deduplicated when a stable source identity or content hash is available.

## Repository rules

- `main` is the accepted minimal product state.
- Feature work must stay on its requested branch until explicitly merged.
- Keep the runtime and GPT action surface minimal.
- Never commit credentials, Application Passwords, connection keys, nonces, purchase codes, tunnel secrets, or local output.
- Update GPT schema/instructions whenever the public REST contract changes.
- Keep plugin version, GPT Action schema version, and runtime contract synchronized.
- Run PHP syntax lint and the Elementize contract checks before considering a change complete.
