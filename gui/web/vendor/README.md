<!-- OWNERSHIP=Claude -->
# Vendored third-party assets

## chessground 9.2.1

Lichess's chess board UI, **GPL-3.0-or-later** (full text in `LICENSE.chessground`).
Upstream: <https://github.com/lichess-org/chessground>

Only the board is chessground's. Everything drawn around it — the sidebar, the mode
selector, the move list, the FEN input — is ours (`gui/web/app.js`, `gui/web/style.css`)
and stays freely changeable. No chess rule is decided here: legality, SAN and game end
all come from the C++ server.

### How these files got here (not part of `make gui`)

```
npm pack chessground          # → chessground-9.2.1.tgz, then untar
cp package/dist/chessground.min.js         gui/web/vendor/
cp package/assets/chessground.base.css     gui/web/vendor/
cp package/assets/chessground.brown.css    gui/web/vendor/
cp package/assets/chessground.cburnett.css gui/web/vendor/
cp package/LICENSE                         gui/web/vendor/LICENSE.chessground
```

`dist/chessground.min.js` is upstream's own prebuilt ESM bundle — it has no imports and
no runtime dependencies, so no bundler is involved. The three CSS files carry the board
squares and all 12 piece images as embedded `data:` URIs, so the page needs no network.

Files are copied verbatim. To update, repeat the commands above with a new version and
bump the version in this file.

## Why the board is drawn from CSS, and what that costs

Both stylesheets above paint with **CSS backgrounds**, not `<img>` elements:
`chessground.brown.css` gives `cg-board` a light `background-color` with the dark
squares as a `background-image`, and `chessground.cburnett.css` gives each
`piece.<role>.<colour>` its artwork as a `background-image`. That is what makes the
page work with no network, but it also puts the board squarely in the path of every
browser feature that recolours a page.

A browser dark mode — Chrome/Edge's *Auto Dark Mode for Web Contents*, or a Dark
Reader-style extension — walks a page it believes is light and darkens it: CSS
colours get remapped, and each background image is judged light or dark and the dark
ones are inverted. On this board that lands exactly wrong. The white pieces are
mostly `#fff`, so they are judged light and left alone; the black pieces are mostly
`#000`, so they are judged dark and inverted — **white bodies, and the two sides
become indistinguishable** (issue #19). The squares flip with them: the light
`background-color` is darkened while the dark-square SVG is inverted to a lighter
tone, so the checker pattern comes out in reverse.

Nothing is wrong with the SVGs; they are correct `data:` URIs and render right in a
browser that leaves them alone. The fix is to stop the browser from deciding, since
`gui/web/style.css` is already a dark theme:

- `:root { color-scheme: dark }` plus `<meta name="color-scheme" content="dark">` —
  the standard "this page handles dark itself" signal. Chrome/Edge auto dark mode
  stands down on it.
- `<meta name="darkreader-lock">` in `gui/web/index.html` — Dark Reader ignores
  `color-scheme`, and this is its own documented opt-out.

**Swapping the piece set does not help.** Any set has dark black pieces, so any set
gets inverted the same way; the meta tags are the fix, and a new set inherits it. If
a future set is ever added, keep those two declarations — and if the pieces still
read wrong, the extension is ignoring both, so check it before touching the CSS.
