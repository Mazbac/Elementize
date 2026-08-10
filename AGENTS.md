# Elementize development rules

Elementize has one narrow product scope: guarded content editing for existing Elementor + Pixfort pages.

## In scope

- Read existing Elementor pages.
- Read/write recognized text/copy fields.
- Read/write recognized link destinations.
- Swap recognized image fields with verified WordPress image attachments.
- Swap recognized Pixfort icon fields.
- Media Library lookup and current-conversation image import.
- Setup/onboarding for a Custom GPT.
- Stale-state guards, mandatory revisions, persisted save verification, and verified rollback attempts.

## Out of scope

Do not add page generation, template selection/insertion, section removal, page lifecycle controls, design settings, color/typography/spacing controls, browser automation, screenshots, Ollama, aesthetic scoring, visual QA, repair loops, semantic ranking, or autonomous layout/design behavior unless the product owner explicitly changes scope.

## Mutation rules

Never expose unrestricted Elementor JSON writes. A content mutation must be based on a fresh read and preserve:

1. exact page ID;
2. expected page title/status;
3. fresh Elementor content hash;
4. exact element ID;
5. exact setting path;
6. expected current value/attachment;
7. a successful pre-change WordPress revision;
8. verification against persisted Elementor `_elementor_data`, not only an in-memory document object;
9. rollback attempt and rollback verification when save verification fails.

Reject duplicate targets in a single mutation request. Do not mutate shared/global/embedded template documents or dynamic/global values. If revisions are unavailable, fail closed instead of writing without a recovery point.

## Repository rules

- `main` is the current product state.
- Keep the runtime small; remove dead experiments instead of leaving dormant feature stacks.
- Keep the public GPT contract at the minimum action surface needed for the product.
- Never commit credentials, Application Passwords, connection keys, nonces, purchase codes, tunnel secrets, or local audit output.
- Update the GPT schema and instructions when the public REST contract changes.
- Run PHP syntax lint and the Elementize contract checks before considering a change complete.
