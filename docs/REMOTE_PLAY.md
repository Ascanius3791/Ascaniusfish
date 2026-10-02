<!-- OWNERSHIP=Claude -->
# Playing over the internet, at no hosting cost (M7, issue #27)

The engine and the GUI server (`make gui`) stay on your machine. A friend
elsewhere reaches them through a tunnel that turns your local port into an
HTTPS URL, and a token (printed once, new every run) is what keeps that URL
from being an open door. Nothing here is hosted anywhere — the tunnel just
forwards; if you stop the server or your PC sleeps, the game stops too.

GitHub Pages is a dead end on its own: a page served from there is HTTPS, and
an HTTPS page cannot POST to your plain-HTTP PC. The tunnel below serves
`gui/web/` itself instead, so that mismatch never comes up.

## Install cloudflared once

[cloudflared](https://developers.cloudflare.com/cloudflared/) is a single
binary, no account or domain needed. On Debian/Ubuntu/WSL:

```
curl -L -o cloudflared.deb https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb
sudo dpkg -i cloudflared.deb
```

Check it's on your PATH: `cloudflared --version`.

## The one command

```
make gui-remote
```

This builds and starts the server, launches a Cloudflare quick tunnel for it,
and prints the combined link once the tunnel is up (a few seconds):

```
Ascaniusfish GUI on http://localhost:8173  (serving gui/web, Ctrl-C to stop)
Token for this run: 3f9a1c7b2e4d5f60...
Local link: http://localhost:8173/?token=3f9a1c7b2e4d5f60...
Starting a Cloudflare quick tunnel (cloudflared)...
Shareable link: https://some-random-words.trycloudflare.com/?token=3f9a1c7b2e4d5f60...
(quick tunnels can take a while to actually route through Cloudflare —
 checking now, will print a confirmation once it's actually live)
The page keeps the token in a cookie after the first open. A new run makes a new token.
Tunnel confirmed reachable — the shareable link is live.
```

Send your friend the **Shareable link** line, exactly as printed. That's it —
one command, one link, nothing installed anywhere but your own machine.

**Quick tunnels are genuinely slow to come up sometimes.** cloudflared's own
random `trycloudflare.com` hostname has to propagate through Cloudflare's edge
before it answers anything, which is best-effort with no SLA — it's usually a
few seconds but can occasionally take minutes, and there's nothing this repo
can do to speed that up. The server checks the link itself in the background
(without blocking the **Local link**, which works immediately) and prints
"Tunnel confirmed reachable" once it's actually live; if you send the link
before that line shows up, your friend will just see the page hang on
"connecting" for a while. If nothing shows up after several minutes, either
retry (Ctrl-C and rerun) or fall back to the LAN or Lichess-bot options below.
A *named* Cloudflare Tunnel (a fixed hostname on a domain you own, added to a
Cloudflare account) doesn't have this random-subdomain propagation step and is
what Cloudflare itself documents as the reliable option — not set up here
since it needs a domain in a Cloudflare account first; ask if you want to add
that.

The token gates every route, including every static file — a request without
it (or without the cookie it sets on the first correct one) gets a plain 401
and never touches a session or an engine. Both the token and the tunnel's URL
are new every run, so an old link stops working the moment you restart.

**Who is on it**: the header's counter left of `live` says how many pages
are open on the server (every tab, every session), e.g. `2 online · 1 remote`.
A page counts as remote when it came through the tunnel (cloudflared forwards
from localhost but adds a `Cf-Connecting-Ip` header) or from another machine
on the LAN. It updates as soon as a page opens or closes.

**Ctrl-C stops everything**: the server, every engine it started, and the
tunnel — `gui-remote` owns the `cloudflared` process it launched and kills it
on the way out, so nothing is left forwarding to a server that's gone.

If cloudflared isn't installed, isn't on PATH, or is slow to connect, the
server keeps running regardless and prints why on stderr; the **Local link**
still works for playing on your own machine in the meantime.

## Doing it by hand instead

`gui-remote` is `make gui tunnel=cloudflared` — running the two halves as
separate commands, in separate terminals, is the same thing with more manual
steps and is occasionally useful for seeing cloudflared's own log directly:

```
make gui                                       # terminal 1
cloudflared tunnel --url http://localhost:8173 # terminal 2
```

Combine the printed `https://....trycloudflare.com` URL with the token
yourself: `https://....trycloudflare.com/?token=<token>`. Leave `bind=` at
its default (`127.0.0.1`) either way — `cloudflared` runs on the same machine
and reaches the server over loopback, same as opening it in your own browser.

## Alternative: LAN only, no tunnel

Sharing with someone on the same Wi-Fi doesn't need a tunnel at all:

```
make gui GUI_BIND=0.0.0.0
```

then give them `http://<your-LAN-IP>:8173/?token=...` (find your LAN IP with
`ip addr` or `hostname -I`). The token still gates everything; `bind=0.0.0.0`
only decides who can *reach* the port, same as it would for the tunnel case
above if `cloudflared` weren't doing that hop from loopback instead.

## Fallback: a Lichess BOT account

If the tunnel proves more trouble than it's worth, `ascaniusfish_uci` already
speaks UCI, which is what a
[Lichess BOT account](https://lichess.org/api#tag/Bot) expects behind its
bridge — a friend then just challenges the bot on lichess.org, and hosting is
Lichess's problem instead of yours. Not implemented here yet; this doc covers
the direct route only.
