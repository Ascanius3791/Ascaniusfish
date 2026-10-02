// OWNERSHIP=Claude
#ifndef PRUNING_HPP
#define PRUNING_HPP
// What the search's pruning decisions are made from (#71-#75): a node's static
// eval, and the constants of each pruning rule, kept here rather than in
// lib/Settings.hpp so that the patches to minimax() stay a few lines.
#include "eval.hpp"
#include "nne.hpp"

// A node's static eval on the scale the search's leaves return: eval() plus,
// with UseNNE on, the net's correction (clamped below the TB band, see
// nne::corrected_eval()). The quiet leaf and the stand pat of
// minimax_tactical() and the null-move guard of minimax() all read this one, so
// a bound they compare it with came from the same scale.
// Call it only for a node that is not in check and not over (no legal move):
// in check the side to move can't stand pat, so the number means nothing, and
// eval() is told the game goes on. It costs a full eval() plus, with the net,
// one correction (~1.7k cycles), so compute it once per node and only where a
// consumer needs it.
inline int static_eval(const BB* const pos, const WEIGHTS& W)
{
    const int raw = eval(pos, W, 0);
    return nne::enabled ? nne::corrected_eval(pos, raw) : raw;
}

// Late move pruning (#73): at a shallow, non-PV node a quiet move the ordering
// ranks late is skipped without a search. The move count that counts as late
// grows with depth. Not in check, not for a move that gives check, not near a
// mate or TB score (a skipped move could be the one that mates), and never the
// first move. When moves were skipped and the node fails low (high at a black
// node), lmp_clamp() gives the bound the node can claim: the window edge, not
// the weaker result of the moves that were searched.
#include <climits>
#include <cstdlib>
#include "tb_search.hpp"

constexpr bool ENABLE_LMP = 1;
constexpr int LMP_MAX_DEPTH = 3;

// Moves (counted from 0) with index >= this are late at `depth` (1..LMP_MAX_DEPTH).
constexpr int lmp_move_count(int depth)
{
    return 3 + depth * depth;   // 4, 7, 12 for depths 1..3 -> index >= 4, 7, 12
}

// Is a node with this window one where moves may be pruned? PV nodes (a window
// wider than a null window) and windows touching the TB/mate band are not.
inline bool lmp_window_ok(int alpha, int beta)
{
    return (long long)beta - alpha <= 1
        && std::abs((long long)alpha) < TB_WIN_SCORE
        && std::abs((long long)beta) < TB_WIN_SCORE;
}

// The value to return and store for a node that skipped moves. White (max) node
// that failed low (result <= alpha_0): alpha_0, the skipped moves may have been
// better than what was searched but were not expected to beat alpha. Black node
// that failed high (result >= beta_0): beta_0. Anything else keeps its result.
inline int lmp_clamp(bool white_move, int result, int alpha_0, int beta_0)
{
    if(white_move && result < alpha_0) return alpha_0;
    if(!white_move && result > beta_0) return beta_0;
    return result;
}

#endif // PRUNING_HPP
