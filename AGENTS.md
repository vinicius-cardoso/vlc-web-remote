# Repository Guidelines

## Project Structure & Module Organization

This is a small Node.js ES module app that serves a mobile-first VLC remote and proxies VLC HTTP API calls.

- `src/server.js` starts the HTTP server, serves static files, and exposes `/api/status` and `/api/control`.
- `src/vlcClient.js` contains VLC API requests, command mapping, and status normalization.
- `src/env.js` loads `.env` values without external dependencies.
- `public/index.html`, `public/styles/app.css`, and `public/scripts/app.js` are the browser UI.
- `scripts/` contains Linux helper scripts for launching VLC with HTTP enabled and installing the desktop entry.
- `docs/vlc-setup.md` documents VLC setup and troubleshooting.

## Build, Test, and Development Commands

- `npm start`: run the server with `node src/server.js`.
- `npm run dev`: run the server with Node watch mode for local development.
- `cp .env.example .env`: create local VLC/server configuration.
- `VLC_PASSWORD=sua_senha npm start`: override config for one run.
- `./scripts/start-vlc.sh`: start VLC with the HTTP interface enabled.
- `./scripts/install-open-with-vlc.sh`: install the desktop launcher integration.

There is no build step; the server and static assets run directly on Node 18+.

## Coding Style & Naming Conventions

Use modern JavaScript ES modules. Match the existing style: two-space indentation, double quotes, semicolons, `const`/`let`, and small focused functions. Keep server-side names in English (`getVlcStatus`, `sendControl`) and preserve Portuguese user-facing text in UI messages and docs. Avoid adding runtime dependencies unless they remove clear complexity.

## Testing Guidelines

No automated test framework is configured yet. For behavior changes, manually verify:

- `npm run dev` starts without errors.
- `GET /api/status` returns JSON when VLC HTTP is available.
- UI controls in `public/` still update playback, volume, audio tracks, and subtitles.

If adding tests, prefer Node's built-in `node:test` and place files under `test/` with names like `vlcClient.test.js`.

## Commit & Pull Request Guidelines

This checkout does not include Git history, so follow a simple imperative convention: `Add volume control fallback`, `Fix VLC timeout message`, `Document setup steps`. Keep commits scoped to one concern.

Pull requests should include a short description, manual test results, any `.env` or VLC setup impact, and screenshots or screen recordings for UI changes.

## Security & Configuration Tips

Do not commit `.env` or real VLC passwords. Keep `.env.example` safe and generic. Be careful when changing static file serving, request body parsing, or VLC command mapping, because those paths affect local-network exposure.
