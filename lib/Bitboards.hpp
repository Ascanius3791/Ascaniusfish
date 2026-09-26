// OWNERSHIP=Ascanius
#ifndef BITBOARDS_HPP
#define BITBOARDS_HPP

#include <iostream>
#include <string>
#include "../src/bit_operations.cpp"

// Per-position cache for values that are expensive to (re)compute and get asked for
// repeatedly while one position is under consideration - eval, move ordering and
// tactical scoring all separately want occupancy/attack-maps/check-state for the
// same BB, currently each recomputing it from scratch.
//
// Lifetime contract: a cached value is only valid as long as the owning BB's Board[]
// hasn't changed since it was filled in. copy_BB()/Base_BB() (the two ways the engine
// seeds a BB to represent a position, including into reused buffers like `wfh` arrays)
// call reset() on the destination, so anything built through them starts with an empty
// cache. A raw memberwise copy (e.g. `BB temp = *original;`) followed by directly
// mutating temp.Board[...] does NOT reset it automatically - call temp.lazy.reset()
// yourself before using the getters below on a BB that was patched up that way.
struct BB_lazyfields
{
    uint64_t occupancy=0;          bool occupancy_valid=false;
    uint64_t white_pieces=0;       bool white_pieces_valid=false;
    uint64_t black_pieces=0;       bool black_pieces_valid=false;

    // These need get_bishop_attacks()/get_rook_attacks()/attacked_squares() (src/magics.cpp),
    // in_check() (src/checks.cpp) and one_move() (src/move_generation.cpp) - none of which
    // are declared yet at this point in the header chain. So only the cache slots and the
    // getter *prototypes* live here; the getter bodies are defined in src/BB_lazyfields.cpp,
    // which is only pulled in (via lib/BB_lazyfields.hpp) once those headers are available.
    uint64_t white_attacks=0;      bool white_attacks_valid=false;
    uint64_t black_attacks=0;      bool black_attacks_valid=false;
    int in_check_cache=-1;         // -1 = not yet known, 0 = false, 1 = true
    int has_legal_move_cache=-1;   // -1 = not yet known, 0 = false, 1 = true ("one_move")

    void reset(){ *this = BB_lazyfields(); }
};

struct BB
{
    uint64_t Board[12];
    bool white_move;
    bool castle[2][2]; //first index is color, second index is side 0=black 1=white, 0=queen side, 1=king side
    uint64_t en_passant;

    uint64_t zobrist_hash;
    int move;
    int halfmoves_since_last_capture_or_pawn_move;

    mutable int number_of_repetitions;

    // NOTE: despite the name, copy_BB() does NOT copy this field - it calls lazy.reset()
    // on the destination instead (same for Base_BB()). A "copy" of a position starts with
    // an empty cache rather than inheriting the source's, precisely because both functions
    // are also used to (re)populate reused buffer slots (e.g. wfh[] arrays) that may be
    // carrying a stale cache from whatever position previously lived there.
    mutable BB_lazyfields lazy;

    //constructors
    BB();
    BB(const std::string FEN);
    BB(const BB* const original,std::string mode = "copy");

    //functions

    std::string get_UCI(const BB* const goal);

    std::string get_FEN();

    bool * to_bool_board();//for the neural network


    void check_castling_rights();

    bool operator==(const BB* const rhs) const;

    bool operator==(const BB& rhs) const;

};

void castling_right_rook_correction_for_col(BB* const original, int for_white);// corrects for presence of rooks

void castling_rights(BB* const original);

bool are_equal(const BB* const  BB_1,const BB* const BB_2);

std::string get_coordinate_PGN(std::vector<BB> history);

// BB_lazyfields getters, cheap half (no dependency on later headers, defined right here in Bitboards.cpp).
uint64_t get_occupancy(const BB* const b);
uint64_t get_pieces_of_colour(const BB* const b, bool white);

// BB_lazyfields getters, expensive half - only declared here, defined in src/BB_lazyfields.cpp
// once get_bishop_attacks/get_rook_attacks/attacked_squares, in_check and one_move exist.
uint64_t get_attacked_squares(const BB* const b, bool by_white);
bool get_in_check(const BB* const b);
bool get_has_legal_move(const BB* const b);
    































#endif // BITBOARDS_HPP
