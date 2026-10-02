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

#endif // PRUNING_HPP
