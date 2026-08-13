# Elementize admin frontend

The WordPress admin UI is built with React, TypeScript, Vite, Tailwind CSS, Radix UI primitives, and the local component layer in `src/components/ui/`.

## Development

```bash
cd frontend
npm ci
npm run dev
```

The Vite development server is for frontend work only. WordPress uses the generated production assets.

## Production bundle

```bash
cd frontend
npm ci
npm run build
```

The build writes:

- `assets/admin/elementize-admin.js`
- `assets/admin/elementize-admin.css`
If the production assets are absent, Elementize deliberately falls back to the PHP onboarding/setup screen so a missing frontend build does not block configuration.

## UI conventions

- Prefer the existing local UI primitives before adding another component dependency.
- Keep WordPress/PHP authoritative for capabilities, permissions, nonces, connection state, secrets, and mutations.
- Keep frontend actions typed through `src/types.ts` and `src/lib/actions.ts`.
- Reuse the existing Tailwind token/theme system instead of adding one-off inline styling systems.
- Add a dependency only when it is used by shipped source code; remove abandoned primitives and packages together.
- Generated files in `assets/admin/` are build output. Change source under `frontend/src/`, then rebuild.

## Source layout

```text
src/
  app/          app shell
  components/   shared UI
  lib/          action/client helpers
  pages/        WordPress admin screens
  types.ts      shared frontend contract types
```

`components.json` keeps the shadcn-compatible component configuration for the local UI layer. The shadcn CLI is not a project dependency; invoke it explicitly with `npx` only when intentionally adding a component.
