<!-- OWNERSHIP=Claude -->
# Changelog

Releases of the GUI container (`docs/DEPLOY.md`). Each version says what changed and,
under **Server steps**, what has to be done on the server beyond swapping the image.

## 0.1.0 (unreleased)

The first version for a server: the browser GUI with its engine in one Docker image.

- One shared password with a login page, a signed cookie for 30 days, limits on
  connections, engines, boards and failed logins (#109).
- Docker image, `compose.yaml` template, GitHub workflow building the image, version
  and commit in the page footer and at `/api/version` (#110).
- Each visitor gets their own board by default (a random id, no more shared `main`),
  with copy-link/new-board/delete-board controls and a 24h idle-board cleanup (#112).

### Server steps
- The one-time setup in `docs/DEPLOY.md`: subdomain and certificate, data folder owned
  by 10001, image upload, stack from `compose.yaml`, nginx directives.
