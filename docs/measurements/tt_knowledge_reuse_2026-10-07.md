<!-- OWNERSHIP=Claude -->
# Analysis reuse in any walk order (#100, #101), 2026-10-07

The walks of `docs/plans/tt_knowledge_reuse.md` §5 on the 3.Bc2+ study, run with `tools/walkback`
(`make walkback`): no PTT, no tablebases, NNE as the Session's default, a fresh engine per walk,
1 s per analysed position, key positions until they show their mate (cap 10 s). `mate` is when the
page first showed a mate, `engine` when the engine first sent one, `refresh` how many TT entries
the engine demoted before the search (from step 2 on). The step label of the mixed walk is the
plan's row (`m3` = row 3).

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
