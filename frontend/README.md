# Elementize admin frontend

React + TypeScript + Mantine frontend for the WordPress admin experience.

## Development

```bash
cd frontend
npm install
npm run dev
```

The development server is for component work only. WordPress uses the production bundle.

## Production bundle

```bash
cd frontend
npm ci
npm run build
```

The build writes exactly these runtime assets:

- `assets/admin/elementize-admin.js`
- `assets/admin/elementize-admin.css`

If those assets are absent, Elementize deliberately falls back to the existing PHP onboarding screen so setup is never blocked by a missing frontend build.

## Pure Mantine rule

The WordPress admin frontend is intentionally **pure Mantine**.

- Every visible UI primitive and layout must come from `@mantine/core`.
- Mantine hooks may come from `@mantine/hooks`.
- Do not add custom frontend CSS files.
- Do not add `className` styling hooks to frontend TSX.
- Do not build visible UI with raw layout tags such as `div`, `section`, `nav`, `main`, `aside`, `header`, `footer`, `form`, `input` or `span`.
- Use Mantine components, Mantine style props, Mantine variants and the central Mantine theme instead.
- WordPress/PHP continues to own capabilities, permissions, nonces, connection state and secret generation.

## Approved palette

The theme may use only these Elementize colors:

- `#f2f3f7`
- `#94adbf`
- `#ccafe6`
- `#9854cb`
- `#64379f`
- `#56188f`
- `#584170`
- `#2a1b3a`
- `#1d0432`
