<!-- OWNERSHIP=Claude -->
# Running the GUI on a Plesk server (#110)

One container holds the web server (`gui/ascaniusfish_gui`), the engine it starts
(`ascaniusfish_uci`), the page and the net. Plesk's nginx sits in front: it handles
https for `<subdomain>.<hauptdomain>` and passes requests to the container, which is
reachable only from the server itself (`127.0.0.1:<hostport>`). Everyone logs in with
one shared password (#109). Nothing here deploys by itself: every step on the server
is done by hand.

Placeholders, filled in on the server only, never in the repository:
`<subdomain>`, `<hauptdomain>`, `<datenordner>` (a folder on the server for the
container's data), `<hostport>` (a free port), `<version>` (e.g. `0.1.0`),
`<passwort>`, `<cookie-secret>`.

## Getting an image

1. Add the version to `CHANGELOG.md`, commit, then tag and push:
   `git tag v<version> && git push origin v<version>`.
   (Or on GitHub: Actions › Docker image › Run workflow, with the version.)
2. When the run is green, download the artifact `ascaniusfish-<version>` from the run's
   page. GitHub hands it out as a zip; inside is `ascaniusfish-<version>.tar` (~85 MB),
   the image. It is kept for 14 days.

Built locally instead: `docker build --build-arg ENGINE_COMMIT=$(git rev-parse --short HEAD)
--build-arg APP_VERSION=<version> -t ascaniusfish:<version> .` and
`docker save ascaniusfish:<version> -o ascaniusfish-<version>.tar`.

## One-time setup

1. **Subdomain**: in Plesk, add `<subdomain>.<hauptdomain>` and give it a Let's Encrypt
   certificate (SSL/TLS Certificates), with http redirected to https.
2. **Data folder**: the container does not run as root but as user number 10001, so
   that user must own its folder. Over SSH, as root:
   `mkdir -p <datenordner> && chown 10001:10001 <datenordner>`.
   The engine's long searches are kept there (`.ascaniusfish/ptt.txt`) across updates.
3. **Image**: Docker › Images › Upload, choose the `.tar` (unzipped). Over SSH:
   `docker load -i ascaniusfish-<version>.tar`.
4. **Stack**: Docker › Stacks › Add stack, paste `compose.yaml` from the repository and
   replace every `<...>`. Make the password and the secret new and random, e.g. each
   from `openssl rand -hex 32` (the secret needs at least 16 characters). Start it.
5. **nginx**: in the subdomain's Apache & nginx Settings, switch **Proxy mode** off
   (nothing goes to Apache) and put this in **Additional nginx directives**:
   ```nginx
   location ~ ^/ {
       proxy_pass http://127.0.0.1:<hostport>;
       proxy_http_version 1.1;
       proxy_set_header Host $host;
       proxy_set_header X-Real-IP $remote_addr;
       proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
       proxy_set_header X-Forwarded-Proto $scheme;
       proxy_buffering off;
   }
   ```
   `~ ^/` instead of a plain `/` keeps clear of the `location /` Plesk writes itself.
   `proxy_buffering off` lets the live updates (engine lines, moves) through at once.

## Checks

On the server:
- `curl -s http://127.0.0.1:<hostport>/api/version` prints
  `{"version":"<version>","commit":"…","engineVersion":"<version>",…}`. That route is
  the only one without a login. Both versions equal means the server and the engine
  come from the same build.
- The container's log (Docker › Containers › the container › Logs) ends with
  `Password login on, client address and https from the proxy's headers.` Without
  `Password login on`, `GUI_PASSWORD` did not arrive; the server then prints a token
  instead and must not stay online like that.

From outside:
- `https://<subdomain>.<hauptdomain>/` shows the login page; after logging in, the
  footer at the bottom right shows `v<version> · <commit>`.
- Port `<hostport>` on the server's public address does not answer from another machine.
- Analyse › Turn engine on shows engine lines within a second or two.

## Updating

1. Build the new version (see above) and upload its `.tar` (step 3).
2. Read the new version's **Server steps** in `CHANGELOG.md` and do them.
3. In the stack, change `image: ascaniusfish:<version>` to the new version and apply
   (recreate). Logins survive: they depend only on the password and the secret.
4. Run the checks; `/api/version` must show the new version.
5. Keep the previous image until the new one has run for a while. To go back, put the
   old version into the stack again. Then delete images older than that.

## What the container is allowed

From `compose.yaml`: one CPU, 1 GB memory, 256 processes, a read-only system with only
`/tmp` (16 MB, in memory) and `/data` writable, no Linux capabilities, no way to gain
privileges. Each engine (`GUI_MAX_ENGINES`, default 2) uses under 100 MB, most of it the 64 MB
transposition table. The
tablebases are not in the image, so endgames are searched, not looked up.
