# Elementize development rules

Elementize has one narrow product scope: guarded content editing for existing Elementor + Pixfort pages.

## In scope

- Read existing Elementor pages.
- Read/write recognized text and link fields.
- Swap recognized image fields with verified WordPress image attachments.
- Search the Media Library.
- Import one current-conversation image from an allowed OpenAI file host.
- Import one direct public HTTPS image with safe URL/MIME/size/dimension checks and provenance metadata.
- Search the installed Pixfort icon library and write exact verified Line, Duotone, or Solid values.
- Setup/onboarding for a Custom GPT.
- Stale-state guards, mandatory revisions, persisted save verification, and verified rollback attempts.

## Out of scope

Do not add page generation, template selection/insertion, section removal, page lifecycle controls, design settings, color/typography/spacing controls, browser automation, screenshots, Ollama, aesthetic scoring, visual QA, repair loops, semantic ranking, or autonomous layout/design behavior unless the product owner explicitly changes scope.

## Mutation rules

Never expose unrestricted Elementor JSON writes. A mutation must use a fresh page read, exact page identity/state, exact element ID and setting path, expected current value/attachment, a successful pre-change revision, persisted `_elementor_data` verification, and verified rollback attempts. Reject duplicate targets, dynamic/global values, and shared template writes.

Pixfort icon mutations must use values verified against the installed Pixfort icon index. Remote image imports must be public HTTPS only, use WordPress safe HTTP validation, and remain bounded by file-size/dimension limits.

## Repository rules

- `main` is the accepted minimal product state.
- Feature work must stay on its requested branch until explicitly merged.
- Keep the runtime and GPT action surface minimal.
- Never commit credentials, Application Passwords, connection keys, nonces, purchase codes, tunnel secrets, or local output.
- Update GPT schema/instructions whenever the public REST contract changes.
- Run PHP syntax lint and the Elementize contract checks before considering a change complete.
