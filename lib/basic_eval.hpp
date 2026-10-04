// OWNERSHIP=Ascanius
#ifndef BASIC_EVAL_HPP
#define BASIC_EVAL_HPP
#include "Bitboards.hpp"
#include "Weights.hpp"
#include "../src/templates.cpp"
#include "../src/magics.cpp"
#include "../src/printing.cpp"
#include "../src/BB_lazyfields.cpp" // get_occupancy/get_attacked_squares/... used by sorting_eval/tactical_potential

float enemy_material_left_percent(const BB* const original, bool for_white);

bool side_to_move_lacks_non_pawn_material(const BB* const original);// zugzwang guard for null-move pruning

// The game phase: both sides' pieces, knight and bishop 1, rook 2, queen 4, pawns and
// kings not counted, at most PHASE_MAX (all pieces on). Every eval term with an opening
// and an endgame value blends them by it: (opening*phase + endgame*(PHASE_MAX-phase))/PHASE_MAX.
constexpr int PHASE_MAX = 24;
int game_phase(const BB* const original);

int piecetable(const BB* const original , const WEIGHTS& W = WEIGHTS_OG);

int piece_activity_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);

float distance_to_king(int king_sq, int other_sq);// returns the distance of a sqare, to the kings square

int king_safety_of_colour(const BB* const original,bool white, const WEIGHTS& W =WEIGHTS_OG);

inline int material_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);

int pawn_struckture_eval_of_colour(const BB* const original, bool white, const WEIGHTS& W = WEIGHTS_OG);//positive is good for both colours

int positional_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);

// A passed pawn has no enemy pawn ahead of it on its own or a neighbouring
// file (#84). The helpers are shared with gui/eval_split.hpp.
uint64_t passed_pawns_of_colour(const BB* const original, bool white);
int passed_pawn_bonus(int relative_rank, int phase, const WEIGHTS& W = WEIGHTS_OG);// cp for one passer, phase = game_phase()
// One passer's score in parts, its own side's view (#90): the base bonus and the
// endgame modifiers (phase as above). PASSER_SQUARE counts only for the side's best
// passer, so passed_pawn_eval() takes its maximum rather than the sum.
enum Passer_Part { PASSER_BASE, PASSER_PATH, PASSER_KING, PASSER_SUPPORT, PASSER_ROOK, PASSER_SQUARE, PASSER_PARTS };
void passed_pawn_parts(const BB* const original, bool white, int sq, int phase, const WEIGHTS& W, int part[PASSER_PARTS]);
int passed_pawn_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);// white's view

int tempo_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);// the side to move's tempo, white's view (#70)

int basic_eval(const BB*const original , const WEIGHTS& W = WEIGHTS_OG);// return the evaluation in centipawns

int tactical_potential(const BB* const original, int king_safety_white, int king_safety_black, const WEIGHTS& W=WEIGHTS_OG);

int sorting_eval(const BB* const original, const WEIGHTS& W =WEIGHTS_OG);// accelerates pruning this function has to be lightheaded(Quick to compute)





















#endif // EVAL_HPP
