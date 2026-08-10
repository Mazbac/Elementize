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
npm install
npm run build
```

The build writes exactly these runtime assets:

- `assets/admin/elementize-admin.js`
- `assets/admin/elementize-admin.css`

If those assets are absent, Elementize deliberately falls back to the existing PHP onboarding screen so setup is never blocked by a missing frontend build.

## Design rules

Use only the approved Elementize palette:

- `#f2f3f7`
- `#94adbf`
- `#ccafe6`
- `#9854cb`
- `#64379f`
- `#56188f`
- `#584170`
- `#2a1b3a`
- `#1d0432`

The React layer owns presentation. WordPress/PHP continues to own capabilities, permissions, nonces, connection state and secret generation.
