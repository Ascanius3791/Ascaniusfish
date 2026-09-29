<!-- OWNERSHIP=Claude -->
# Syzygy prober: memory and speed (2026-09-30)

Code: `lib/syzygy.hpp` at 4209626 (issue #37), built with the default flags.
Command: `make syzygy-test` (tables `~/syzygy-nr`: 145 WDL + 145 DTZ files, 3-5 pieces, exact "no rounding" DTZ).
Machine: WSL2, files on the ext4 disk, page cache cold for most of the tables.

| Measure | Result |
|---|---|
| `init()`, all 290 files mapped, headers not yet read | 6.5 ms |
| RSS after `init()` | 5500 kB (+88 kB) |
| RSS after one cold WDL probe of every table | 17064 kB (+11.7 MB) |
| RSS after one cold DTZ probe of every table | 28636 kB (+23.2 MB) |
| `probe_wdl`, first probe of a table | 692 us |
| `probe_wdl`, warm | 4.4 us per position |
| `probe_dtz`, first probe of a table | 2177 us |
| `probe_dtz`, warm | 14.4 us per position |

Correctness in the same run: 1039 / 1039 Lichess reference positions match (WDL and DTZ exactly);
43500 random positions, 0 mismatches (DTZ against its children: 32259 exact, 1 one ply off, within the check's tolerance).

Opening costs well under the 10 MB RSS target. The +11.7 / +23.2 MB above are pages read by the probes
(headers, and the blocks the probed positions sit in), not by opening. The cold times include the disk read.
