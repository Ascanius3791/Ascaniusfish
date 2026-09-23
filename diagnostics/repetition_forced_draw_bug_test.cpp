// OWNERSHIP=Claude
// Regression test for a real bug found via live play: minimax()'s repetition/
// cycle detection used to be an unconditional, node-level early return - as soon
// as ANY repeat or "one reversible move away" cycle was detected for the CURRENT
// node, the whole node was scored as an immediate draw (0), before any move was
// even generated. This meant a completely winning position (e.g. queen+knight vs
// bare king) got scored as a draw the moment a reversible shuffle move was merely
// *available*, regardless of whether the side to move would ever actually choose
// it over an obviously better alternative.
//
// Fixed by moving both checks (exact repeat, and the cuckoo-cycle "one reversible
// move away" heuristic) out of the node-entry early return and into the per-move
// loop: each candidate move that would recreate an earlier position is scored as
// a draw *for that one move only*, fed into the normal alpha-beta max/min
// alongside every other candidate - so a better move still wins if one exists.
//
// This test constructs the exact bug pattern by hand: an ancestor position A0
// (black to move, white massively ahead - queen+knight vs bare king), then a real
// 3-ply sequence (black shuffles its king out and back, sandwiching a single
// white knight move) that lands on a position B3 where white, to move, has the
// knight move available that would exactly recreate A0 - precisely the
// "cuckoo cycle" pattern. With the bug, minimax() from B3 would return eval==0.
// Fixed, it must instead reflect white's overwhelming material.
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
    Zobrist dummy_zobrist_init; // constructing this populates the static key tables, mirroring main()'s `Zobrist new_zobrist = Zobrist();`

    // A0: black to move, white massively ahead (Q+N vs bare king).
    BB A0; build("4k3/8/8/8/8/2N5/8/2K4Q b - - 0 1", &A0);
    // B1: after black plays Ke8-d8.
    BB B1; build("3k4/8/8/8/8/2N5/8/2K4Q w - - 1 1", &B1);
    // B2: after white plays Nc3-b1 (the move that will need undoing later).
    BB B2; build("3k4/8/8/8/8/8/8/1NK4Q b - - 2 1", &B2);
    // B3 (current node): after black plays Kd8-e8, undoing its own first move.
    // White to move here, and Nb1-c3 would exactly recreate A0.
    BB B3; build("4k3/8/8/8/8/8/8/1NK4Q w - - 3 1", &B3);

    expect(!are_equal(&B3, &A0), "sanity: B3 must not already equal A0 (knight still on b1)");

    CuckooCycleTable cycle_table;
    int found_piece_type; Move found_move;
    bool cycle_detected = cycle_table.detect_upcoming_cycle(B3, A0, 3, found_piece_type, found_move);
    expect(cycle_detected, "sanity: detect_upcoming_cycle should find Nb1-c3 recreates A0 (gap=3)");

    BB path_history[8];
    path_history[0] = A0;
    path_history[1] = B1;
    path_history[2] = B2;
    // path_history[3] would be B3 itself - minimax() writes this on entry.

    BB* wfh = new BB[4000];
    PV_Line result = minimax(&B3, wfh, /*depth=*/4, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr, path_history, /*ply=*/3, &cycle_table);

    std::cout << "eval from B3 (white massively ahead, cycle available): " << result.eval << "\n";
    expect(result.eval != 0, "BUG: position scored as a draw despite white being massively ahead");
    expect(result.eval > 500, "expected an eval strongly reflecting white's Q+N material edge, got " + std::to_string(result.eval));

    std::cout << "repetition_forced_draw_bug_test passed\n";
    return 0;
}
