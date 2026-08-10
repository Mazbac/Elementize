# Elementize

Elementize is a guarded WordPress/Elementor/Pixfort design and page-building engine intended to be used by AI agents without exposing unrestricted Elementor JSON or unsafe page mutations.

## Repository layout

- `elementize.php` — WordPress plugin entry point.
- `includes/` — runtime PHP modules. This will be the next cleanup target after local smoke verification.
- `config/` — non-runtime configuration and agent integration material.
- `tools/` — local diagnostics, visual, repair, and acceptance harnesses.
- `docs/` — active project notes, architecture, process, history, and archives.
- `AGENTS.md` — repository operating rules for AI-assisted development.

## Development principles

- Treat `main` as accepted/known-good state.
- Use focused branches for meaningful work.
- Keep page mutation guarded, reversible, and stale-state protected.
- Keep page creation draft-only unless publishing is explicitly requested.
- Never commit credentials, cookies, nonces, purchase keys, HAR captures, proprietary Pixfort/Essentials source, or local audit artifacts.
- Core visual QA must remain usable with free/local software.

## Validation

GitHub Actions run PHP/INC syntax linting and the GPT Builder contract on every branch push and pull request.

Local harnesses are grouped under `tools/` by purpose.

## Start here

1. `docs/project/status.md`
2. `docs/project/discovery.md`
3. `docs/architecture/design-intelligence.md`
4. `docs/process/fast-build-os.md`
5. `AGENTS.md`

Custom GPT configuration lives under `config/gpt/`.
Historical status snapshots and superseded setup notes live under `docs/history/` and must not be treated as current runtime truth.
