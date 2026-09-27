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
