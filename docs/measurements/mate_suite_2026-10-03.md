<!-- OWNERSHIP=Claude -->
# Mate suite: the mate search as the root's verifier (#78)

Base `ed5cb72` plus #78's two soundness fixes (null-move cutoffs and quiescence
claim no unproven mate), then with the mate search checking every mate the root
claims (`UCI_Engine::verify_mate()`). Suite `tools/mate_suite.epd`: 430 Lichess
mate puzzles (seed 78, NbPlays >= 100), the position after the opponent's first
move; every N confirmed by Stockfish 15 (4M nodes, 4 threads; 1 of 431 judged
rejected). One `go movetime 1000` per position, 3 at a time, single-threaded engines.

```
./tools/mate_suite run                        # make mate-suite
./tools/mate_suite claims movetime=1000       # the bench positions
./tools/mate_suite claims positions=<400 random puzzle FENs> movetime=1000
```

## Hit rate (a hit: `mate N`, N the shortest)

| mate in | positions | before | after | median ms to the mate, before / after |
|---|---|---|---|---|
| 2 | 100 | 100 | 100 | 1 / 1 |
| 3 | 100 | 100 | 100 | 3 / 4 |
| 4 | 100 | 100 | 100 | 14 / 17 |
| 5 | 100 | 98 (2 report mate 6) | **100** | 34 / 84 |
| 6 | 10 | 8 | 8 | 169 / 341 |
| 7 | 10 | 6 (1 reports mate 8) | 7 | 368 / 828 |
| 8 | 10 | 4 | 4 | 420 / 669 |

None too short, of the wrong sign, or with a move that does not mate in N,
before or after. The time to the mate grows by the proof that nothing is
shorter (full width below the mate found). The misses at 6-8 are positions the
root search never claims a mate in: a verifier cannot find those.

## The root's claims

| positions | claims | confirmed (the quickest) | refuted | unchecked |
|---|---|---|---|---|
| the suite | 433 | 416 (403) | **0** | 17 |
| bench (30) | 3 | 2 | **0** | 1 |
| 400 random Lichess puzzles, any theme | 27 | 23 | **0** | 4 |

Unchecked: out of budget (max(100k nodes, the nodes the deepening spent)) or
time. Most are early iterations of a claim a later one confirms; the TT keeps
what each check proved.

## The mate search alone (`diagnostics/mate_search_test.cpp`)

Full width, 3M nodes per position: exact on all 400 mates in 2-5 and 9/10 in 6
(7 and 8 mostly out of budget); checks-only finds the shortest in 100/96/90/82
of 100 (N = 2..5) within a few thousand nodes. Every line it gives mates in
exactly N plies. Speed: the mates in 5 take 5.4 s in all (median 31 ms, the
longest 0.6 s) at about 1.7M nodes/s.

## The quiescence search's mates (`diagnostics/quiescence_mate_test.cpp`)

From every suite position, every check and every check after a check and a
reply: 32,367 positions in check, `minimax_tactical()` run at
`forced_moves_left` 0..2. It returned 2,102 mate scores and full width
confirmed all 2,102.
