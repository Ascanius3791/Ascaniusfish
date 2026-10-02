<!-- OWNERSHIP=Claude -->
# MultiPV: does line 1 at K>1 match K=1? (#69)

Commit: issue/69, on top of 683b98f (`minimax()` excluded root moves).
Command: `diagnostics/multipv_agreement depth=D k=…` (build line in its header),
the 30 bench positions (one has no legal moves, so 29 count). Per position
and K: TT and killers cleared, iterative deepening 1..D through
`multipv_search()`, the function the UCI loop calls. "Agrees" = line 1's
first move is K=1's best move.

| depth | K   | line 1 agrees | mean \|score diff\| | nodes vs K=1 | time vs K=1 |
|-------|-----|---------------|---------------------|--------------|-------------|
| 6     | 2   | 25/29         | 12.4 cp             | ×2.11        | ×2.08       |
| 6     | 3   | 26/29         | 12.5 cp             | ×3.07        | ×3.22       |
| 6     | 5   | 23/29         | 11.9 cp             | ×4.56        | ×5.06       |
| 6     | all | 25/29         | 26.7 cp             | ×19.19       | ×21.56      |
| 8     | 2   | 25/29         | 3.0 cp              | ×2.08        | ×2.18       |
| 8     | 3   | 26/29         | 3.6 cp              | ×3.01        | ×3.26       |
| 8     | 5   | 21/29         | 9.8 cp              | ×4.42        | ×4.73       |
| 8     | all | 25/29         | 6.7 cp              | ×19.43       | ×21.67      |

Line 1 can differ only through the TT entries passes 2..K leave below the
root. Most of the disagreements are near-ties: at depth 8 the other move
scores within a few cp of K=1's (e.g. a2a3/f2f3 both +0.40, a2a4 +0.85 /
a1c1 +0.83). Exceptions: one position (#12, b3c2 +0.88 against g2g4 +0.74
at K=2) and K=5 on #13 (f4f6 +1.18 against +0.18), where an extra pass's
TT entries changed what the next depth saw. The cost is about K plain
searches; at K≥2 the extra passes miss the root's TT move and TT cutoff.
