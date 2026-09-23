// OWNERSHIP=Claude
#ifndef SEE_HPP
#define SEE_HPP
#include "Bitboards.hpp"
#include "Weights.hpp"
#include "../src/templates.cpp"
#include "../src/magics.cpp"

struct AttackersOfSquare
{
    uint64_t white;
    uint64_t black;
};

// All pieces of each color attacking `square`, given a caller-supplied occupancy
// (not necessarily the board's live occupancy - callers shrink it during a swap-off
// to reveal x-ray attackers behind sliders).
AttackersOfSquare attackers_of(int square, uint64_t occupancy, const uint64_t Board[12]);

// Classic gain-array static exchange evaluation for a capture from `from_square` to
// `to_square`. Returns the net material result (positive = the capturing side comes
// out ahead) assuming both sides always recapture with their least valuable attacker.
int static_exchange_eval(const uint64_t Board[12], int from_square, int to_square,
                          bool white_to_move, bool is_en_passant, const WEIGHTS& W = WEIGHTS_OG);

bool is_capturing_move(const uint64_t Board[12], int to_square, bool white_to_move, bool is_en_passant);

// For call sites that already have a Move (from/to/is_en_passant known directly).
bool is_good_capture(const BB* const original, int from_square, int to_square,
                      bool is_en_passant, const WEIGHTS& W = WEIGHTS_OG);

// For call sites that only have a parent/child BB pair (from/to reconstructed via
// the own-side occupancy diff).
bool is_good_capture_from_child(const BB* const parent, const BB* const child, const WEIGHTS& W = WEIGHTS_OG);

// A promoting pawn move - independent of whether it's also a capture. Takes the raw
// promotion_piece_type field rather than a Move (Move isn't declared yet at this point
// in the include chain - see.cpp is pulled in before move_generation.cpp in
// ascaniusfish.hpp). -1 means "no promotion"; move-generation never sets 0 either
// (real piece types start at 1), so ">0" and "!=-1" are equivalent here.
inline bool is_promotion(int promotion_piece_type)
{
    return promotion_piece_type > 0;
}

#endif // SEE_HPP
