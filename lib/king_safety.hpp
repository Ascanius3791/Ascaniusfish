// OWNERSHIP=Claude
#ifndef KING_SAFETY_HPP
#define KING_SAFETY_HPP
#include "Bitboards.hpp"
#include "../src/templates.cpp"
#include "../src/magics.cpp"
#include "Weights.hpp"

// King safety (#42): what makes a king unsafe, rather than how crowded it is.
// Two halves, both from the defending side's point of view:
//
// - Attack: enemy knights, bishops, rooks and queens whose attacks reach the
//   king ring (the 3x3 block around the king, pushed off the edge), plus one
//   attacker per ring square an enemy pawn attacks and not two own pawns defend
//   (#102). One piece there is harmless, so fewer than ks_min_attackers
//   attackers score nothing.
//   From two on, danger units add up (attacker weights, hits next to the king,
//   weak ring squares, safe checks) and the penalty is danger^2 / ks_danger_div,
//   so every piece that joins costs more than the one before.
// - Shelter: own pawns in front of the king on its file and the two beside it
//   (files clamped to b..g). A pawn still on its second rank covers fully, an
//   advanced one less, a missing one not at all; an open file costs extra, and
//   so do enemy pawns storming those files. Scaled by the enemy's piece
//   material, since cover matters only while there is something to attack with.
//
// The shape follows the classical (pre-NNUE) Stockfish king safety; the numbers
// are hand-set starting values in centipawns, not tuned. They are the ks_*
// fields of a weight set (#85, lib/weight_set.hpp), set 1 in weights/w1.txt.

struct King_Safety_Detail
{
    int attackers;       // enemy N/B/R/Q whose attacks reach the ring, + ring squares enemy pawns attack (#102)
    int danger_units;    // before the attacker gate and the square
    int attack_penalty;  // >= 0, what the attack half costs
    int shelter_penalty; // >= 0, what the shelter half costs (after material scaling)
};

// Both halves for one side's king (`white` = the white king).
King_Safety_Detail king_safety_detail(const BB* const original, bool white, const WEIGHTS& W = WEIGHTS_OG);

// Each side's king safety as a value <= 0: 0 = safe, below 0 = the (attack +
// shelter) penalty. What sorting_eval()/tactical_potential() read as "king in
// danger". Computes both sides' attack maps once.
void king_safety_of_both(const BB* const original, int& white, int& black, const WEIGHTS& W = WEIGHTS_OG);

// white's king safety minus black's: > 0 is good for white, like the rest of
// basic_eval().
int king_safety_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);

#include "../src/king_safety.cpp"

#endif // KING_SAFETY_HPP
