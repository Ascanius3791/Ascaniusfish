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

// Fixed material values (P,R,N,B,Q,K board order = 1,5,3,3,9) for SEE and
// capture ordering, independent of WEIGHTS: ordering tactical moves should
// maximise material, not the tuned eval. The king never gets captured in an
// exchange (it only recaptures when nothing can take it back), so its value
// never counts.
constexpr int SEE_PIECE_VALUE[6] = {1, 5, 3, 3, 9, 100};
constexpr int QUEEN_PROMOTION = 4;  // promotion_piece_type of a queen promotion (= queen's board index)

// Classic gain-array static exchange evaluation of the move `from_square` ->
// `to_square`: the net material result for the mover (in SEE_PIECE_VALUE units)
// assuming both sides keep taking on to_square with their least valuable
// attacker, and may stop whenever continuing would lose. The move need not be a
// capture (a quiet queen promotion scores its gain minus what the swap-off
// costs). A pawn reaching the last rank - the mover or a recapturer - becomes
// `promotion_piece_type` (the mover) or a queen (recapturers; also the mover
// when promotion_piece_type <= 0). The king recaptures only as the last piece,
// when the other side has nothing left that attacks the square.
int static_exchange_eval(const uint64_t Board[12], int from_square, int to_square,
                          bool white_to_move, bool is_en_passant, int promotion_piece_type = -1);

bool is_capturing_move(const uint64_t Board[12], int to_square, bool white_to_move, bool is_en_passant);

// A capture that doesn't lose material (SEE >= 0, so equal trades count).
// The WEIGHTS parameter is ignored (SEE uses SEE_PIECE_VALUE); it stays so the
// existing call sites compile unchanged.
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
