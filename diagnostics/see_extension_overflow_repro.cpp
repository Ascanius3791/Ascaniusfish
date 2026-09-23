// OWNERSHIP=Claude
// Regression test for a heap-buffer-overflow ("double free or corruption") that
// originally surfaced when SEE-based capture extensions (is_good_capture(), see
// src/see.cpp) were wired into minimax()'s recursion as a depth EXTENSION. That
// heuristic has since been removed entirely: minimax()'s depth==0 leaf now
// delegates to minimax_tactical() (ascaniusfish_2.hpp), a dedicated capture-only
// search that recurses only into moves for which is_good_capture() holds. Its
// recursion is hard-bounded (see max_non_king_pieces, ascaniusfish_2.hpp): every
// step removes exactly one non-king piece from the board, and there are at most
// 30 non-king pieces on the board ever, so no heuristic cap is needed. The
// live-play arena is sized off depth+max_non_king_pieces (see
// Play::nicely_written_play()) accordingly. This test builds that exact arena
// size and runs a real engine_move() at depth 6 from the start position - build
// with -fsanitize=address to verify there's no overflow (a plain build may not
// crash every run even when a bug is present, since it depends on adjacent heap
// layout).
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <iostream>

int main()
{
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB position;
    initialize_FEN_to::Standartboard(position.Board);
    position.white_move = 1;
    position.en_passant = 0;
    position.move = 1;
    position.halfmoves_since_last_capture_or_pawn_move = 0;
    castling_rights(&position);

    const int depth = 6;
    const int arena_size = 80 * (depth + max_non_king_pieces); // matches Play::nicely_written_play()
    BB* wfh = new BB[arena_size];

    Play engine;
    std::cout << "calling engine_move at depth " << depth << " with arena size " << arena_size << "...\n";
    int eval = engine.engine_move(&position, wfh, false, depth, WEIGHTS_OG, nullptr);
    std::cout << "returned eval: " << eval << "\n";
    return 0;
}
