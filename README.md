# Elementize

Elementize is a guarded WordPress/Elementor/Pixfort design and page-building engine intended to be used by AI agents without exposing unrestricted Elementor JSON or unsafe page mutations.

## Repository layout

- `elementize.php` — WordPress plugin entry point.
- `includes/` — runtime PHP modules. See `includes/README.md` for the module map.
- `tools/` — local acceptance, diagnostics, visual, and repair harnesses. See `tools/README.md`.
- `docs/` — architecture, process, historical milestones, and archived material.
- `PROJECT.md` — active project outcome, constraints, and backlog.
- `DISCOVERY.md` — durable technical findings from source/runtime investigation.
- `AGENTS.md` — repository operating rules for AI-assisted development.
- `WP_BUILDER_INSTRUCTIONS.md` — current Custom GPT operating instructions.
- `CUSTOM_GPT_ACTIONS.openapi.yaml` — current GPT Actions schema.
- `CUSTOM_GPT_ACTIONS_0.6_APPEND.yaml` — current schema extension consumed by CI when required.

## Development principles

- Treat `main` as accepted/known-good state.
- Use focused branches for meaningful work.
- Keep page mutation guarded, reversible, and stale-state protected.
- Keep page creation draft-only unless publishing is explicitly requested.
- Never commit credentials, cookies, nonces, purchase keys, HAR captures, proprietary Pixfort/Essentials source, or local audit artifacts.
- Core visual QA must remain usable with free/local software.

## Validation

GitHub Actions run PHP/INC syntax linting and the GPT Builder contract on branch pushes and pull requests.

Local acceptance harnesses are grouped under `tools/` by purpose.

## Documentation

Start with:

1. `PROJECT.md`
2. `DISCOVERY.md`
3. `docs/architecture/design-intelligence.md`
4. `docs/process/fast-build-os.md`
5. `AGENTS.md`

Historical status snapshots and superseded setup notes live under `docs/history/` and must not be treated as current runtime truth.
