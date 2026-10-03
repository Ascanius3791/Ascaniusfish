<!-- OWNERSHIP=Claude -->
# Third-party code

Vendored as published upstream: the files carry no ownership marker and are not
edited here. A fix goes upstream or into a wrapper of ours, not into these files.

## gaviota/ — Gaviota tablebase prober (#77)

- Upstream: https://github.com/michiguel/Gaviota-Tablebases (master, fetched 2026-10-03),
  Miguel A. Ballicora, MIT (`gaviota/license.txt`). The decompressors it bundles are
  zlib (zlib licence), LZMA SDK (public domain), LZF (BSD) and its own Huffman coder.
- Kept: the prober and its decompressors. Dropped: upstream's build files, the example
  `tbprobe.c` and the 3-piece test set.
- Built by our `Makefile` with `gcc` into `third_party/gaviota/libgtb.a` and linked into
  the UCI engine; `lib/gaviota.hpp` is our interface to it.
- The tables (DTM, 3-5 pieces, `*.gtb.cp4`, 7.0 GB) are not in the repo:
  `~/gaviota`, from https://tablebase.lichess.ovh/tables/standard/Gaviota/.
