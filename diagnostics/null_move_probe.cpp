// OWNERSHIP=Claude
// Sanity/regression checks for null-move pruning (see minimax()'s null-move
// guard in ascaniusfish_2.hpp, and side_to_move_lacks_non_pawn_material() in
// src/basic_eval.cpp). Covers: the zugzwang-guard helper in isolation, a
// material-crushing position (the kind null-move should actually cut off on)
// still reporting a strongly correct eval, and a short forced mate still
// being found correctly with null-move pruning active throughout the tree.
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void expect(bool condition, const std::string& message)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

static void setup()
{
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
}

static void build(const std::string& fen, BB* board)
{
    FEN_to_BB(fen, board);
    castling_rights(board);
    board->zobrist_hash = Zobrist::compute_Zobrist_Hash(*board);
}

int main()
{
    setup();
    Zobrist dummy_zobrist_init; // populates the static Zobrist key tables

    // (1) Zugzwang-guard helper, in isolation.
    {
        BB kp_vs_kp; build("4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1", &kp_vs_kp);
        expect(side_to_move_lacks_non_pawn_material(&kp_vs_kp),
               "(1a) white to move with only K+P should be flagged as lacking non-pawn material");

        BB kp_vs_kp_black; kp_vs_kp_black = kp_vs_kp; kp_vs_kp_black.white_move = 0;
        expect(side_to_move_lacks_non_pawn_material(&kp_vs_kp_black),
               "(1b) black to move with only K+P should also be flagged");

        BB white_has_rook; build("4k3/4p3/8/8/8/8/4P3/R3K3 w - - 0 1", &white_has_rook);
        expect(!side_to_move_lacks_non_pawn_material(&white_has_rook),
               "(1c) white to move with a rook present must NOT be flagged");

        BB black_has_knight_white_to_move; build("3nk3/4p3/8/8/8/8/4P3/4K3 w - - 0 1", &black_has_knight_white_to_move);
        expect(side_to_move_lacks_non_pawn_material(&black_has_knight_white_to_move),
               "(1d) the guard checks the SIDE TO MOVE's own material, not the opponent's - white to move here still lacks non-pawn material despite black's knight");
    }

    BB* wfh = new BB[4000];

    // (2) Material-crushing position (Q+N vs bare king) - exactly the shape
    // null-move pruning should cut off on. With ENABLE_NULL_MOVE_PRUNING
    // active for the whole tree, the reported eval must still strongly and
    // correctly reflect white's material edge, not some corrupted bound.
    {
        BB pos; build("4k3/8/8/8/8/2N5/8/2K4Q w - - 0 1", &pos);
        PV_Line result = minimax(&pos, wfh, /*depth=*/5, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
        std::cout << "(2) Q+N vs bare king, depth 5, eval: " << result.eval << "\n";
        expect(result.eval > 500, "(2) expected an eval strongly reflecting white's Q+N material edge, got " + std::to_string(result.eval));
    }

    // (3) Short forced mate (Ra1-a8#, black king boxed in by its own pawns)
    // searched at a nominal depth well past the mate itself, so null-move
    // pruning fires throughout the surrounding non-mating branches. The top
    // result must still be the exact mate: eval==INT_MAX and PV move a1-a8.
    {
        BB pos; build("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", &pos);
        PV_Line result = minimax(&pos, wfh, /*depth=*/5, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
        std::cout << "(3) back-rank mate-in-1, depth 5, eval: " << result.eval
                   << ", PV move from=" << result.moves[0].from << " to=" << result.moves[0].to << "\n";
        // Mate scores are encoded relative to INT_MAX by distance-to-mate (see
        // interpret_eval() in ascaniusfish.hpp), so a mate found a few plies
        // below the nominal search depth isn't exactly INT_MAX - same
        // "proven mate" check minimax() itself uses for TT readouts.
        expect(result.eval >= INT_MAX - max_mating_seq, "(3) expected a proven mate for white (near INT_MAX), got " + std::to_string(result.eval));
        expect(result.current_lenght>=1 && result.moves[0].from==0 && result.moves[0].to==56,
               "(3) expected the PV's first move to be Ra1-a8");
    }

    std::cout << "null_move_probe passed\n";
    return 0;
}
