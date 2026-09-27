// OWNERSHIP=Claude
// Does reusing a BB via FEN_to_BB (which never calls lazy.reset()) leave a stale cache?
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"
#include <iostream>
int main()
{
    initialize_rand(); Zobrist z; init_magics();
    init_sliders_attacks(1); init_sliders_attacks(0);

    const char* a = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const char* b = "4k3/8/8/8/8/8/6q1/4K3 w - - 0 1"; // white IS in check here

    BB board;
    FEN_to_BB(a, &board); castling_rights(&board);
    // warm every lazy field on position a
    volatile uint64_t occ = board.get_occupancy();
    volatile uint64_t wp  = board.get_pieces_of_colour(true);
    volatile uint64_t wa  = board.get_attacked_squares(true);
    volatile bool chk     = board.get_in_check();
    volatile bool lm      = board.get_has_legal_move();
    std::cout << "position a: in_check=" << (int)chk << " occ=" << occ << "\n";

    // reuse the SAME BB for position b, exactly as FEN_to_BB callers do
    FEN_to_BB(b, &board); castling_rights(&board);

    const bool cached_check = board.get_in_check();
    const bool raw_check    = in_check(board.Board, board.white_move);
    const uint64_t cached_occ = board.get_occupancy();
    uint64_t raw_occ = 0; for(int i=0;i<12;i++) raw_occ |= board.Board[i];
    const uint64_t cached_wp = board.get_pieces_of_colour(true);
    uint64_t raw_wp = 0; for(int i=0;i<6;i++) raw_wp |= board.Board[i];

    std::cout << "position b: cached in_check=" << (int)cached_check
              << "  raw in_check=" << (int)raw_check << "\n";
    std::cout << "position b: cached occ=" << cached_occ << "  raw occ=" << raw_occ << "\n";
    std::cout << "position b: cached white=" << cached_wp << "  raw white=" << raw_wp << "\n";

    int bad = (cached_check!=raw_check) + (cached_occ!=raw_occ) + (cached_wp!=raw_wp);
    std::cout << (bad ? "*** STALE CACHE after FEN_to_BB reuse ***" : "no staleness observed") << "\n";
    return bad!=0;
}
