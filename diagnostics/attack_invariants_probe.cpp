// OWNERSHIP=Claude
#include <iostream>
#include <cstdint>
#include <cassert>
#include <vector>

#include "../lib/Bitboards.hpp"
#include "../lib/Bitboard_initialisations.hpp"

static uint64_t sq(int s) { return 1ULL << s; }

int main() {
    BB pos;
    initialize_FEN_to::empty(pos.Board);
    pos.white_move = true;
    pos.en_passant = 0;
    pos.castle[0][0] = pos.castle[0][1] = pos.castle[1][0] = pos.castle[1][1] = false;

    pos.Board[0] = sq(28); // e4
    uint64_t white_attacks = attacks_by_col(pos.Board, true);
    assert((white_attacks & sq(27)) != 0); // d5
    assert((white_attacks & sq(29)) != 0); // f5

    initialize_FEN_to::empty(pos.Board);
    pos.Board[6] = sq(27); // d5
    uint64_t black_attacks = attacks_by_col(pos.Board, false);
    assert((black_attacks & sq(26)) != 0); // c4
    assert((black_attacks & sq(28)) != 0); // e4

    initialize_FEN_to::empty(pos.Board);
    pos.Board[1] = sq(0); // a1
    uint64_t rook_attacks = attacks_by_col(pos.Board, true);
    assert((rook_attacks & sq(8)) != 0);  // a2
    assert((rook_attacks & sq(16)) != 0); // a3

    std::cout << "attack invariants passed\n";
    return 0;
}
