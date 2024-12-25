#ifndef EVAL_HPP
#define EVAL_HPP
#include "Bitboards.hpp"
#include "Weights.hpp"
#include "../src/templates.cpp"
#include "../src/magics.cpp"
#include "../src/printing.cpp"

float enemy_material_left_percent(const BB* const original, bool for_white);

int piecetable(const BB* const original , const WEIGHTS W = WEIGHTS_OG);

int piece_activity_eval(const BB* const original, const WEIGHTS W = WEIGHTS_OG);

float distance_to_king(int king_sq, int other_sq);// returns the distance of a sqare, to the kings square

int king_safety_of_colour(const uint64_t Board[12],bool white, const WEIGHTS W =WEIGHTS_OG);

inline int material_eval(const BB* const original, const WEIGHTS W = WEIGHTS_OG);

int central_pawn_presence(const BB* const original, bool white, const WEIGHTS W = WEIGHTS_OG);//positive is good for both colours

int pawn_struckture_eval_of_colour(const BB* const original, bool white, const WEIGHTS W = WEIGHTS_OG);//positive is good for both colours

int positional_eval(const BB* const original, const WEIGHTS W = WEIGHTS_OG);

int basic_eval(const BB*const original , const WEIGHTS W = WEIGHTS_OG);// return the evaluation in centipawns

int tactical_potential(const uint64_t Board[12], WEIGHTS W=WEIGHTS_OG);

int sorting_eval(const BB* const original, const WEIGHTS W =WEIGHTS_OG);// accelerates pruning this function has to be lightheaded(Quick to compute)





















#endif // EVAL_HPP