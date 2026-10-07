<!-- OWNERSHIP=Claude -->
# Analysis reuse in any walk order (#100, #101), 2026-10-07

The walks of `docs/plans/tt_knowledge_reuse.md` §5 on the 3.Bc2+ study, run with `tools/walkback`
(`make walkback`): no PTT, no tablebases, NNE as the Session's default, a fresh engine per walk,
1 s per analysed position, key positions until they show their mate (cap 10 s). `mate` is when the
page first showed a mate, `engine` when the engine first sent one, `refresh` how many TT entries
the engine demoted before the search (from step 2 on). The step label of the mixed walk is the
plan's row (`m3` = row 3).

## Reading

- **on**, steps back: plies 10', 9, 8 show their mate within 0.0 s, as the engine-off walk does
  (baseline: none; each step showed its own older, deeper root entry). Ply 7 has none in either
  walk (no search has refuted 4...Kg4 yet). After step 2 alone the engine already sent the
  mate at once, but the page kept the Analysis_Store's deeper score until step 4.
- **off**: unchanged, `refresh 0` at every step: nothing is demoted when every parent is
  searched after its child.
- **fwd**: a mate at all 14 steps before and after.
- **mixed**: rows 6, 8, 9 show a mate within 0.0 s (baseline: none at rows 6 and 8). Row 10
  (back to the start through demoted entries) reaches depth 16 in 1 s against 13 in the baseline.
- Forward steps reach the baseline's depth within 1-2 plies; the baseline itself varies by as
  much between runs. The bench signature stays 31041: game searches are unchanged.

## Baseline

`issue/101` = main 73d1f0d + 1252d70 (TTWalk follows exact entries only), `bench` 31041.
Command: `./tools/walkback walks=off,on,fwd,mixed`.

```
off: the engine off but at the key positions
  key    ply 11  mate 0.6 s   -M9      depth 19  engine 0.6 s  
  key    ply 11  mate 2.2 s   -M7      depth 14  engine 2.2 s  
off: back, 1.0 s per step
  back   ply 10  mate 0.0 s   +M8      depth  5  engine 0.0 s  
  back   ply  9  mate 0.0 s   -M10     depth 13  engine 0.0 s  
  back   ply  8  mate 0.1 s   +M11     depth  5  engine 0.1 s  
  back   ply  7  no mate      -2.00    depth 12  engine -      
off: 3 of 4 steps back showed a mate

on: the engine on everywhere, 1.0 s per position
  fwd    ply  0  no mate      +0.00    depth 13  engine -      
  fwd    ply  1  no mate      +8.27    depth 14  engine -      
  fwd    ply  2  no mate      -9.82    depth 14  engine -      
  fwd    ply  3  no mate      +10.50   depth 14  engine -      
  fwd    ply  4  no mate      -11.12   depth 14  engine -      
  fwd    ply  5  no mate      +22.53   depth 13  engine -      
  fwd    ply  6  no mate      -24.82   depth 13  engine -      
  fwd    ply  7  no mate      +39.47   depth 16  engine -      
  fwd    ply  8  no mate      -30.72   depth 18  engine -      
  fwd    ply  9  no mate      +31.70   depth 19  engine -      
  fwd    ply 10  mate 0.4 s   +M10     depth 22  engine 0.4 s  
  key    ply 11  mate 0.0 s   -M9      depth  1  engine 0.0 s  
  side   ply 10  mate 0.0 s   +M10     depth 22  engine 0.0 s    (kept)
  side   ply  9  no mate      +31.70   depth 19  engine -      
  side   ply 10  no mate      +15.35   depth 12  engine -      
  key    ply 11  mate 5.7 s   -M7      depth 15  engine 5.7 s  
on: back, 1.0 s per step
  back   ply 10  no mate      +15.35   depth 12  engine 0.0 s  
  back   ply  9  no mate      +31.70   depth 19  engine -      
  back   ply  8  no mate      -31.70   depth 20  engine -      
  back   ply  7  no mate      +31.70   depth 18  engine -      
on: 0 of 4 steps back showed a mate

fwd: the engine off to the M9, then on along its mate and a transposition, 1.0 s per position
  key    ply 11  mate 0.7 s   -M9      depth 19  engine 0.7 s  
  mate   ply 12  mate 0.0 s   +M9      depth  6  engine 0.0 s  
  mate   ply 13  mate 0.0 s   -M8      depth  6  engine 0.0 s  
  mate   ply 14  mate 0.0 s   +M8      depth  6  engine 0.0 s  
  mate   ply 15  mate 0.0 s   -M7      depth 64  engine 0.0 s  
  mate   ply 16  mate 0.0 s   +M7      depth 64  engine 0.0 s  
  mate   ply 17  mate 0.0 s   -M6      depth 64  engine 0.0 s  
  mate   ply 18  mate 0.0 s   +M6      depth 64  engine 0.0 s  
  mate   ply 19  mate 0.0 s   -M5      depth 64  engine 0.0 s  
  mate   ply 20  mate 0.0 s   +M4      depth 64  engine 0.0 s  
  mate   ply 21  mate 0.0 s   -M3      depth 64  engine 0.0 s  
  swap   ply 14  mate 0.0 s   +M1      depth 64  engine 0.0 s  
  swap   ply 15  mate 0.0 s   -M7      depth 64  engine 0.0 s  
  swap   ply 16  mate 0.0 s   +M7      depth 64  engine 0.0 s    (kept)
  swap   ply 17  mate 0.0 s   -M6      depth 64  engine 0.0 s    (kept)
fwd: 14 of 14 steps showed a mate

mixed: jumps, a side line and revisits, 1.0 s per position
  m1     ply  0  no mate      +0.00    depth 13  engine -      
  m1     ply  1  no mate      +8.27    depth 14  engine -      
  m1     ply  2  no mate      -9.82    depth 14  engine -      
  m1     ply  3  no mate      +10.50   depth 14  engine -      
  m1     ply  4  no mate      -10.57   depth 15  engine -      
  m1     ply  5  no mate      +23.07   depth 13  engine -      
  m1     ply  6  no mate      -23.07   depth 13  engine -      
  m1     ply  7  no mate      +39.47   depth 16  engine -      
  m1     ply  8  no mate      -30.30   depth 18  engine -      
  m1     ply  9  no mate      +31.79   depth 19  engine -      
  m1     ply 10  mate 0.3 s   +M10     depth 22  engine 0.3 s  
  key    ply 11  mate 0.0 s   -M9      depth  1  engine 0.0 s  
  m3     ply  9  no mate      +31.79   depth 19  engine -      
  m4     ply 10  no mate      +14.42   depth 12  engine -      
  key    ply 11  mate 2.7 s   -M7      depth 14  engine 2.7 s  
  m6     ply  8  no mate      -31.79   depth 20  engine -      
  m7     ply 10  mate 0.0 s   +M3      depth 14  engine 0.0 s  
  m8     ply  9  no mate      +31.79   depth 19  engine -      
  m9     ply 10  mate 0.0 s   +M10     depth  6  engine 0.0 s  
  m10    ply  0  no mate      +0.00    depth 13  engine -      
mixed: 2 of 7 steps showed a mate
```

## Step 2: the engine refreshes its path

`UCI_Table::refresh_path()` before every `go infinite`, `store_proven()` after a verified root mate,
the seed guard. `bench` 31041. Same command.

```
off: the engine off but at the key positions
  key    ply 11  mate 0.6 s   -M9      depth 19  engine 0.6 s    refresh 0
  key    ply 11  mate 2.0 s   -M7      depth 14  engine 2.0 s    refresh 0
off: back, 1.0 s per step
  back   ply 10  mate 0.0 s   +M8      depth  6  engine 0.0 s    refresh 0
  back   ply  9  mate 0.0 s   -M10     depth 13  engine 0.0 s    refresh 0
  back   ply  8  mate 0.0 s   +M11     depth  6  engine 0.0 s    refresh 0
  back   ply  7  no mate      -2.00    depth 12  engine -        refresh 0
off: 3 of 4 steps back showed a mate

on: the engine on everywhere, 1.0 s per position
  fwd    ply  0  no mate      +0.00    depth 13  engine -        refresh 0
  fwd    ply  1  no mate      +8.27    depth 14  engine -        refresh 2
  fwd    ply  2  no mate      -10.67   depth 14  engine -        refresh 2
  fwd    ply  3  no mate      +11.04   depth 15  engine -        refresh 2
  fwd    ply  4  no mate      -11.04   depth 14  engine -        refresh 2
  fwd    ply  5  no mate      +22.89   depth 12  engine -        refresh 1
  fwd    ply  6  no mate      -23.07   depth 13  engine -        refresh 1
  fwd    ply  7  no mate      +38.78   depth 15  engine -        refresh 1
  fwd    ply  8  no mate      -30.72   depth 18  engine -        refresh 2
  fwd    ply  9  no mate      +31.14   depth 18  engine -        refresh 2
  fwd    ply 10  mate 0.6 s   +M10     depth 21  engine 0.6 s    refresh 2
  key    ply 11  mate 0.0 s   -M9      depth  1  engine 0.0 s    refresh 0
  side   ply 10  mate 0.0 s   +M10     depth 21  engine 0.0 s    refresh 0  (kept)
  side   ply  9  no mate      +31.14   depth 18  engine -        refresh 0
  side   ply 10  no mate      +15.81   depth 12  engine -        refresh 1
  key    ply 11  mate 2.9 s   -M7      depth 15  engine 2.9 s    refresh 1
on: back, 1.0 s per step
  back   ply 10  no mate      +15.81   depth 12  engine 0.0 s    refresh 0
  back   ply  9  no mate      +31.14   depth 18  engine 0.0 s    refresh 0
  back   ply  8  mate 0.0 s   +M11     depth  6  engine 0.0 s    refresh 0
  back   ply  7  no mate      +38.78   depth 15  engine -        refresh 0
on: 1 of 4 steps back showed a mate

fwd: the engine off to the M9, then on along its mate and a transposition, 1.0 s per position
  key    ply 11  mate 0.8 s   -M9      depth 19  engine 0.8 s    refresh 0
  mate   ply 12  mate 0.0 s   +M9      depth  6  engine 0.0 s    refresh 0
  mate   ply 13  mate 0.0 s   -M8      depth  6  engine 0.0 s    refresh 0
  mate   ply 14  mate 0.0 s   +M8      depth 64  engine 0.0 s    refresh 0
  mate   ply 15  mate 0.0 s   -M7      depth 64  engine 0.0 s    refresh 0
  mate   ply 16  mate 0.0 s   +M7      depth 64  engine 0.0 s    refresh 0
  mate   ply 17  mate 0.0 s   -M6      depth 64  engine 0.0 s    refresh 0
  mate   ply 18  mate 0.0 s   +M6      depth 64  engine 0.0 s    refresh 0
  mate   ply 19  mate 0.0 s   -M5      depth 64  engine 0.0 s    refresh 0
  mate   ply 20  mate 0.0 s   +M4      depth 64  engine 0.0 s    refresh 0
  mate   ply 21  mate 0.0 s   -M3      depth 64  engine 0.0 s    refresh 0
  swap   ply 14  mate 0.0 s   +M1      depth 64  engine 0.0 s    refresh 0
  swap   ply 15  mate 0.0 s   -M7      depth 64  engine 0.0 s    refresh 0
  swap   ply 16  mate 0.0 s   +M7      depth 64  engine 0.0 s    refresh 0  (kept)
  swap   ply 17  mate 0.0 s   -M6      depth 64  engine 0.0 s    refresh 0  (kept)
fwd: 14 of 14 steps showed a mate

mixed: jumps, a side line and revisits, 1.0 s per position
  m1     ply  0  no mate      +0.00    depth 13  engine -        refresh 0
  m1     ply  1  no mate      +8.27    depth 14  engine -        refresh 2
  m1     ply  2  no mate      -10.67   depth 14  engine -        refresh 2
  m1     ply  3  no mate      +11.04   depth 15  engine -        refresh 2
  m1     ply  4  no mate      -11.04   depth 14  engine -        refresh 2
  m1     ply  5  no mate      +23.07   depth 13  engine -        refresh 1
  m1     ply  6  no mate      -24.82   depth 13  engine -        refresh 2
  m1     ply  7  no mate      +38.78   depth 15  engine -        refresh 1
  m1     ply  8  no mate      -30.06   depth 18  engine -        refresh 2
  m1     ply  9  no mate      +31.79   depth 19  engine -        refresh 2
  m1     ply 10  mate 0.5 s   +M10     depth 22  engine 0.5 s    refresh 2
  key    ply 11  mate 0.0 s   -M9      depth  1  engine 0.0 s    refresh 0
  m3     ply  9  no mate      +31.79   depth 19  engine -        refresh 0
  m4     ply 10  no mate      +17.27   depth 13  engine -        refresh 1
  key    ply 11  mate 3.1 s   -M7      depth 15  engine 3.1 s    refresh 2
  m6     ply  8  mate 0.0 s   +M11     depth 13  engine 0.0 s    refresh 0
  m7     ply 10  mate 0.0 s   +M3      depth 64  engine 0.0 s    refresh 0
  m8     ply  9  no mate      +31.79   depth 19  engine 0.0 s    refresh 1
  m9     ply 10  mate 0.0 s   +M10     depth  6  engine 0.0 s    refresh 0
  m10    ply  0  no mate      +2.72    depth 15  engine -        refresh 0
mixed: 3 of 7 steps showed a mate
```

## Step 4: the GUI prefers a mate

A mate beats a kept score however deep (`Analysis_Store::covers()`, `PTT_Store::better()`), and a
fresh result of equal depth replaces the kept one. Same engine, `bench` 31041. Same command.

```
off: the engine off but at the key positions
  key    ply 11  mate 0.7 s   -M9      depth 19  engine 0.7 s    refresh 0
  key    ply 11  mate 2.0 s   -M7      depth 14  engine 2.0 s    refresh 0
off: back, 1.0 s per step
  back   ply 10  mate 0.0 s   +M8      depth  6  engine 0.0 s    refresh 0
  back   ply  9  mate 0.0 s   -M10     depth 13  engine 0.0 s    refresh 0
  back   ply  8  mate 0.0 s   +M11     depth  6  engine 0.0 s    refresh 0
  back   ply  7  no mate      -2.00    depth 12  engine -        refresh 0
off: 3 of 4 steps back showed a mate

on: the engine on everywhere, 1.0 s per position
  fwd    ply  0  no mate      +0.00    depth 13  engine -        refresh 0
  fwd    ply  1  no mate      +8.27    depth 14  engine -        refresh 2
  fwd    ply  2  no mate      -9.82    depth 14  engine -        refresh 2
  fwd    ply  3  no mate      +11.28   depth 15  engine -        refresh 2
  fwd    ply  4  no mate      -11.28   depth 14  engine -        refresh 2
  fwd    ply  5  no mate      +23.07   depth 13  engine -        refresh 1
  fwd    ply  6  no mate      -24.48   depth 12  engine -        refresh 2
  fwd    ply  7  no mate      +37.88   depth 14  engine -        refresh 1
  fwd    ply  8  no mate      -40.84   depth 17  engine -        refresh 2
  fwd    ply  9  no mate      +31.14   depth 18  engine -        refresh 2
  fwd    ply 10  mate 0.6 s   +M10     depth 22  engine 0.6 s    refresh 2
  key    ply 11  mate 0.0 s   -M9      depth  1  engine 0.0 s    refresh 0
  side   ply 10  mate 0.0 s   +M10     depth 22  engine 0.0 s    refresh 0  (kept)
  side   ply  9  no mate      +31.14   depth 18  engine -        refresh 0
  side   ply 10  no mate      +16.48   depth 13  engine -        refresh 1
  key    ply 11  mate 3.7 s   -M7      depth 14  engine 3.7 s    refresh 1
on: back, 1.0 s per step
  back   ply 10  mate 0.0 s   +M8      depth  6  engine 0.0 s    refresh 0
  back   ply  9  mate 0.0 s   -M10     depth 10  engine 0.0 s    refresh 0
  back   ply  8  mate 0.0 s   +M11     depth  6  engine 0.0 s    refresh 0
  back   ply  7  no mate      +37.88   depth 14  engine -        refresh 0
on: 3 of 4 steps back showed a mate

fwd: the engine off to the M9, then on along its mate and a transposition, 1.0 s per position
  key    ply 11  mate 0.7 s   -M9      depth 19  engine 0.7 s    refresh 0
  mate   ply 12  mate 0.0 s   +M9      depth  6  engine 0.0 s    refresh 0
  mate   ply 13  mate 0.0 s   -M8      depth  6  engine 0.0 s    refresh 0
  mate   ply 14  mate 0.0 s   +M8      depth  6  engine 0.0 s    refresh 0
  mate   ply 15  mate 0.0 s   -M7      depth 64  engine 0.0 s    refresh 0
  mate   ply 16  mate 0.0 s   +M7      depth 64  engine 0.0 s    refresh 0
  mate   ply 17  mate 0.0 s   -M6      depth 64  engine 0.0 s    refresh 0
  mate   ply 18  mate 0.0 s   +M6      depth 64  engine 0.0 s    refresh 0
  mate   ply 19  mate 0.0 s   -M5      depth 64  engine 0.0 s    refresh 0
  mate   ply 20  mate 0.0 s   +M4      depth 64  engine 0.0 s    refresh 0
  mate   ply 21  mate 0.0 s   -M3      depth 64  engine 0.0 s    refresh 0
  swap   ply 14  mate 0.0 s   +M1      depth 64  engine 0.0 s    refresh 0
  swap   ply 15  mate 0.0 s   -M7      depth 64  engine 0.0 s    refresh 0
  swap   ply 16  mate 0.0 s   +M7      depth 64  engine 0.0 s    refresh 0  (kept)
  swap   ply 17  mate 0.0 s   -M6      depth 64  engine 0.0 s    refresh 0  (kept)
fwd: 14 of 14 steps showed a mate

mixed: jumps, a side line and revisits, 1.0 s per position
  m1     ply  0  no mate      +0.00    depth 13  engine -        refresh 0
  m1     ply  1  no mate      +8.27    depth 14  engine -        refresh 2
  m1     ply  2  no mate      -10.67   depth 14  engine -        refresh 2
  m1     ply  3  no mate      +10.71   depth 15  engine -        refresh 2
  m1     ply  4  no mate      -10.71   depth 14  engine -        refresh 2
  m1     ply  5  no mate      +23.07   depth 13  engine -        refresh 1
  m1     ply  6  no mate      -25.05   depth 13  engine -        refresh 2
  m1     ply  7  no mate      +28.71   depth 16  engine -        refresh 1
  m1     ply  8  no mate      -30.72   depth 18  engine -        refresh 2
  m1     ply  9  no mate      +20.61   depth 19  engine -        refresh 2
  m1     ply 10  mate 0.0 s   +M10     depth  6  engine 0.0 s    refresh 1
  key    ply 11  mate 0.0 s   -M9      depth  1  engine 0.0 s    refresh 0
  m3     ply  9  no mate      +20.61   depth 19  engine -        refresh 0
  m4     ply 10  no mate      +15.70   depth 12  engine -        refresh 1
  key    ply 11  mate 1.9 s   -M7      depth 14  engine 1.9 s    refresh 1
  m6     ply  8  mate 0.0 s   +M11     depth 14  engine 0.0 s    refresh 0
  m7     ply 10  mate 0.0 s   +M3      depth 64  engine 0.0 s    refresh 0
  m8     ply  9  mate 0.0 s   -M10     depth  7  engine 0.0 s    refresh 1
  m9     ply 10  mate 0.0 s   +M10     depth  7  engine 0.0 s    refresh 0
  m10    ply  0  no mate      +0.13    depth 16  engine -        refresh 0
mixed: 4 of 7 steps showed a mate
```

## Step 6: Patch A, and the live search shown at once

Patch A (98f4dca, approved by Ascanius): in `insert()` a proof replaces an unproven entry
however deep, and only a deeper proof replaces a proof. `bench` stays 31041. On top, the page
shows the live search at once over a kept result however deep (only a kept mate stays up until
the search has one), so the rebuild after a path refresh is visible. Same command.

- **on**, forward: ply 10 shows its mate at 0.0 s (0.4-0.6 s in the three runs before), and the
  second key position (M7) takes 1.0 s (2.0-5.7 s before): the key searches' inner proofs now
  survive under deeper entries.
- Steps back and **mixed** as in step 4 (rows 6, 8, 9 at 0.0 s, row 10 depth 16). Ply 7 and
  row 10 now show the live search's score (-1.07, +0.59) rather than a kept deeper one.

```
off: the engine off but at the key positions
  key    ply 11  mate 0.4 s   -M9      depth 19  engine 0.4 s    refresh 0
  key    ply 11  mate 1.4 s   -M7      depth 14  engine 1.4 s    refresh 0
off: back, 1.0 s per step
  back   ply 10  mate 0.0 s   +M8      depth  6  engine 0.0 s    refresh 0
  back   ply  9  mate 0.0 s   -M10     depth 14  engine 0.0 s    refresh 0
  back   ply  8  mate 0.0 s   +M11     depth  6  engine 0.0 s    refresh 0
  back   ply  7  no mate      -2.23    depth 13  engine -        refresh 0
off: 3 of 4 steps back showed a mate

on: the engine on everywhere, 1.0 s per position
  fwd    ply  0  no mate      +0.00    depth 13  engine -        refresh 0
  fwd    ply  1  no mate      +9.82    depth 15  engine -        refresh 2
  fwd    ply  2  no mate      -10.50   depth 15  engine -        refresh 2
  fwd    ply  3  no mate      +11.04   depth 15  engine -        refresh 2
  fwd    ply  4  no mate      -10.82   depth 15  engine -        refresh 2
  fwd    ply  5  no mate      +23.97   depth 14  engine -        refresh 1
  fwd    ply  6  no mate      -24.17   depth 14  engine -        refresh 2
  fwd    ply  7  no mate      +39.47   depth 16  engine -        refresh 1
  fwd    ply  8  no mate      -31.86   depth 19  engine -        refresh 2
  fwd    ply  9  no mate      +32.16   depth 20  engine -        refresh 2
  fwd    ply 10  mate 0.0 s   +M10     depth  7  engine 0.0 s    refresh 1
  key    ply 11  mate 0.0 s   -M9      depth  1  engine 0.0 s    refresh 0
  side   ply 10  mate 0.0 s   +M10     depth  7  engine 0.0 s    refresh 0  (kept)
  side   ply  9  no mate      -15.26   depth 13  engine -        refresh 0
  side   ply 10  no mate      +16.90   depth 12  engine -        refresh 1
  key    ply 11  mate 1.0 s   -M7      depth 13  engine 1.0 s    refresh 1
on: back, 1.0 s per step
  back   ply 10  mate 0.0 s   +M8      depth  6  engine 0.0 s    refresh 0
  back   ply  9  mate 0.0 s   -M10     depth 13  engine 0.0 s    refresh 0
  back   ply  8  mate 0.0 s   +M11     depth  6  engine 0.0 s    refresh 0
  back   ply  7  no mate      -1.07    depth 12  engine -        refresh 0
on: 3 of 4 steps back showed a mate

fwd: the engine off to the M9, then on along its mate and a transposition, 1.0 s per position
  key    ply 11  mate 0.4 s   -M9      depth 19  engine 0.4 s    refresh 0
  mate   ply 12  mate 0.0 s   +M9      depth  7  engine 0.0 s    refresh 0
  mate   ply 13  mate 0.0 s   -M8      depth  7  engine 0.0 s    refresh 0
  mate   ply 14  mate 0.0 s   +M8      depth 64  engine 0.0 s    refresh 0
  mate   ply 15  mate 0.0 s   -M7      depth 64  engine 0.0 s    refresh 0
  mate   ply 16  mate 0.0 s   +M7      depth 64  engine 0.0 s    refresh 0
  mate   ply 17  mate 0.0 s   -M6      depth 64  engine 0.0 s    refresh 0
  mate   ply 18  mate 0.0 s   +M6      depth 64  engine 0.0 s    refresh 0
  mate   ply 19  mate 0.0 s   -M5      depth 64  engine 0.0 s    refresh 0
  mate   ply 20  mate 0.0 s   +M4      depth 64  engine 0.0 s    refresh 0
  mate   ply 21  mate 0.0 s   -M3      depth 64  engine 0.0 s    refresh 0
  swap   ply 14  mate 0.0 s   +M1      depth 64  engine 0.0 s    refresh 0
  swap   ply 15  mate 0.0 s   -M7      depth 64  engine 0.0 s    refresh 0
  swap   ply 16  mate 0.0 s   +M7      depth 64  engine 0.0 s    refresh 0  (kept)
  swap   ply 17  mate 0.0 s   -M6      depth 64  engine 0.0 s    refresh 0  (kept)
fwd: 14 of 14 steps showed a mate

mixed: jumps, a side line and revisits, 1.0 s per position
  m1     ply  0  no mate      +0.00    depth 13  engine -        refresh 0
  m1     ply  1  no mate      +10.67   depth 15  engine -        refresh 2
  m1     ply  2  no mate      -9.77    depth 15  engine -        refresh 2
  m1     ply  3  no mate      +10.82   depth 16  engine -        refresh 2
  m1     ply  4  no mate      -10.82   depth 15  engine -        refresh 2
  m1     ply  5  no mate      +22.53   depth 13  engine -        refresh 1
  m1     ply  6  no mate      -24.66   depth 14  engine -        refresh 1
  m1     ply  7  no mate      +29.79   depth 17  engine -        refresh 1
  m1     ply  8  no mate      -31.45   depth 19  engine -        refresh 2
  m1     ply  9  no mate      +20.54   depth 19  engine -        refresh 2
  m1     ply 10  mate 0.0 s   +M10     depth  7  engine 0.0 s    refresh 1
  key    ply 11  mate 0.0 s   -M9      depth  1  engine 0.0 s    refresh 0
  m3     ply  9  no mate      -14.47   depth 13  engine -        refresh 0
  m4     ply 10  no mate      +17.44   depth 13  engine -        refresh 1
  key    ply 11  mate 2.1 s   -M7      depth 15  engine 2.1 s    refresh 1
  m6     ply  8  mate 0.0 s   +M11     depth 14  engine 0.0 s    refresh 0
  m7     ply 10  mate 0.0 s   +M3      depth 64  engine 0.0 s    refresh 0
  m8     ply  9  mate 0.0 s   -M10     depth  7  engine 0.0 s    refresh 0
  m9     ply 10  mate 0.0 s   +M10     depth  7  engine 0.0 s    refresh 0
  m10    ply  0  no mate      +0.59    depth 16  engine -        refresh 0
mixed: 4 of 7 steps showed a mate
```

## Step 7: mate claims are proofs too

`tt_proven()` (`lib/tt_result.hpp`) also counts a **mate claim**: a lower bound in white's mate
band or an upper bound in black's ("the side that mates does at least that well"). An exact
mate's proof is made of such bounds (every defending reply after the first comes back from a
null window), so a claim is exactly as sound; null move, LMP and quiescence cannot make one
(checked in `minimax()`). With Ascanius's approval the probe cuts on a claim at any depth
(`is_mate_claim` in `minimax()`), and through `tt_proven()` `insert()` keeps a claim over an
unproven entry and the path refresh leaves it alone. Bounds on the other side ("no faster mate")
are no proof and keep their depth. Bench unchanged at 31041.

Walks `off,on,fwd,mixed`: the same steps show a mate as in step 6 (off 3/4, on 3/4, fwd 14/14,
mixed 4/7). The M7 key position took 3.7 s (off), 0.1 s (on) and 2.0 s (mixed), against 1.4,
1.0 and 2.1 s in step 6: within the spread of 1 s searches, so the claims change nothing
measurable here.
