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
struct BB;

struct BB_lazyfields
{
    void reset(){ *this = BB_lazyfields(); }

    // Getters: each looks up the cached value if valid, otherwise computes it from
    // `owner` (the BB this BB_lazyfields instance lives on, i.e. the BB whose `lazy`
    // member this is called through - e.g. `owner->lazy.get_occupancy(owner)`) and
    // fills the cache before returning. This is the only way to read the fields
    // below - they are private precisely so nothing can read a stale/unset value
    // or mark a slot valid without actually computing it.
    //
    // Cheap getters: defined right here in src/Bitboards.cpp, no dependency on
    // later headers.
    uint64_t get_occupancy(const BB* const owner);
    uint64_t get_pieces_of_colour(const BB* const owner, bool white);

    // Expensive getters: need get_bishop_attacks()/get_rook_attacks()/attacked_squares()
    // (src/magics.cpp), in_check() (src/checks.cpp) and one_move() (src/move_generation.cpp) -
    // none of which are declared yet at this point in the header chain. So only the
    // prototypes live here; the bodies are defined in src/BB_lazyfields.cpp, which is
    // only pulled in (via lib/BB_lazyfields.hpp) once those headers are available.
    uint64_t get_attacked_squares(const BB* const owner, bool by_white);
    bool get_in_check(const BB* const owner);
    bool get_has_legal_move(const BB* const owner);

private:
    uint64_t occupancy=0;          bool occupancy_valid=false;
    uint64_t white_pieces=0;       bool white_pieces_valid=false;
    uint64_t black_pieces=0;       bool black_pieces_valid=false;

    uint64_t white_attacks=0;      bool white_attacks_valid=false;
    uint64_t black_attacks=0;      bool black_attacks_valid=false;
    int in_check_cache=-1;         // -1 = not yet known, 0 = false, 1 = true
    int has_legal_move_cache=-1;   // -1 = not yet known, 0 = false, 1 = true ("one_move")
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

    // Thin wrappers delegating to `lazy`'s own getters, always passing `this` as the
    // owner - the caller never supplies that pointer, so it can never be mismatched
    // with the `lazy` instance it's read through. See BB_lazyfields above for the
    // cheap/expensive split between where these are defined.
    uint64_t get_occupancy() const;
    uint64_t get_pieces_of_colour(bool white) const;
    uint64_t get_attacked_squares(bool by_white) const;
    bool get_in_check() const;
    bool get_has_legal_move() const;

};

void castling_right_rook_correction_for_col(BB* const original, int for_white);// corrects for presence of rooks

void castling_rights(BB* const original);

bool are_equal(const BB* const  BB_1,const BB* const BB_2);

std::string get_coordinate_PGN(std::vector<BB> history);
































#endif // BITBOARDS_HPP
