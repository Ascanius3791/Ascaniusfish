// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

// End-to-end check of the repetition/cycle wiring in minimax()/engine_move()
// (ascaniusfish_2.hpp): drives Play::engine_move() through the SAME `history`
// global Play::nicely_written_play() itself uses.
//
// All positions below give White an extra rook (a materially SYMMETRIC bare-
// king position can't tell a correct search apart from a bug forcing every
// position to a draw, since a real eval there is also legitimately 0). This
// asymmetry is what a real earlier version of this file lacked, and it's
// exactly why it missed a real bug (found via live play, not by this probe):
// minimax() used to treat "a repeat/cycle is reachable from here" as an
// unconditional, node-level early return, scoring the WHOLE node as a draw
// before generating any moves - regardless of whether the side to move had a
// better option. A completely winning position could get scored as a draw the
// moment ANY reversible shuffle was merely available.
//
// The fix moved both checks (exact repeat, and the cuckoo-cycle "one
// reversible move away" heuristic) into the per-move loop: a candidate move
// that would recreate an earlier position is scored as a draw for THAT move
// only, compared normally against every other candidate via alpha-beta - so a
// genuinely better move still wins. This means eval==0 is only ever correct
// when a draw is actually the side-to-move's best available outcome (see the
// second scenario below, where it's the DISADVANTAGED side holding the
// option) - not an unconditional rule for "a repeat is reachable."

static void expect(bool condition, const std::string& message)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

int main()
{
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    Zobrist zobrist_init = Zobrist();

    // White king steps from wk_from to wk_to and back; black king steps from
    // bk_from to bk_to and back - two independent one-square round trips, far
    // enough apart that the kings are never adjacent and the (stationary)
    // white rook never shares a rank/file with either black king square (so it
    // can never attack/check the black king, keeping every position a normal,
    // ongoing-game-style legal position).
    int wk_from = 0;
    uint64_t wk_dests = K_template[wk_from];
    int wk_to = find_and_delete_trailling_1(wk_dests);

    int bk_from = 63;
    while((K_template[wk_from] | K_template[wk_to] | (1ULL<<wk_from) | (1ULL<<wk_to)) & (1ULL<<bk_from))
    bk_from--;
    uint64_t bk_dests = K_template[bk_from] & ~(K_template[wk_from] | K_template[wk_to] | (1ULL<<wk_from) | (1ULL<<wk_to));
    expect(bk_dests!=0, "test setup: black king needs a step that stays clear of the white king's squares");
    int bk_to = find_and_delete_trailling_1(bk_dests);

    int rook_sq = -1;
    for(int r=0; r<64 && rook_sq==-1; r++)
    {
        if(r==wk_from || r==wk_to || r==bk_from || r==bk_to) continue;
        if(r/8==bk_from/8 || r%8==bk_from%8) continue; // shares a rank/file with C
        if(r/8==bk_to/8   || r%8==bk_to%8)   continue; // shares a rank/file with D
        rook_sq = r;
    }
    expect(rook_sq!=-1, "test setup: should find a rook square that can never attack either black king square");

    BB P0 = BB();
    P0.castle[0][0]=P0.castle[0][1]=P0.castle[1][0]=P0.castle[1][1]=false; // no rooks near either king - no castling rights
    P0.Board[5]  = 1ULL << wk_from; // white king
    P0.Board[1]  = 1ULL << rook_sq; // white rook (stationary throughout)
    P0.Board[11] = 1ULL << bk_from; // black king
    P0.white_move = true;
    P0.halfmoves_since_last_capture_or_pawn_move = 20;
    P0.zobrist_hash = Zobrist::compute_Zobrist_Hash(P0);

    BB P1 = P0; // after White plays Kwk_from-wk_to
    P1.Board[5] = 1ULL << wk_to;
    P1.white_move = false;
    P1.halfmoves_since_last_capture_or_pawn_move = 21;
    P1.zobrist_hash = Zobrist::compute_Zobrist_Hash(P1);

    BB P2 = P1; // after Black plays Kbk_from-bk_to (independent of White's move)
    P2.Board[11] = 1ULL << bk_to;
    P2.white_move = true;
    P2.halfmoves_since_last_capture_or_pawn_move = 22;
    P2.zobrist_hash = Zobrist::compute_Zobrist_Hash(P2);

    BB P3 = P2; // after White plays Kwk_to-wk_from (undoing White's own earlier move)
    P3.Board[5] = 1ULL << wk_from;
    P3.white_move = false;
    P3.halfmoves_since_last_capture_or_pawn_move = 23;
    P3.zobrist_hash = Zobrist::compute_Zobrist_Hash(P3);

    BB* wfh = new BB[2000];
    Play play; // engine_move() is a Play member function

    // --- REGRESSION: a single quiet move (ply_gap=1 relative to the search
    // root) must NOT be treated as a cycle. Undoing the move that was just
    // played is always available for any quiet move - that's not a repeat,
    // it's just what "reversible" means. This is exactly the bug that made
    // the real engine score nearly every quiet move as an instant draw.
    {
        history.clear();
        history.push_back(P0);
        history.push_back(P1);

        BB original = P1;
        lookup_table* table = new lookup_table();
        int insertions_before = table->number_of_inserions;

        int eval = play.engine_move(&original, wfh, false, 3, WEIGHTS_OG, table);

        expect(eval>0, "engine_move: a single quiet move must be evaluated for real (white's extra rook), not forced to a 0 draw");
        expect(table->number_of_inserions>insertions_before, "engine_move: real search should have happened here, inserting into the TT");
        delete table;
    }

    // --- Exact repeat: history=[P0,P1,P0], current=P0 again, White to move.
    // REGRESSION (found via real play, not by this probe): minimax() used to
    // treat "this exact position already occurred once" as an unconditional,
    // node-level early return - scoring the WHOLE position as a draw before
    // even generating moves, regardless of whether the side to move had a
    // better option. Here White (to move, up a rook) obviously has better
    // moves than shuffling the king back toward a repeat (e.g. any rook move),
    // so the search must find and prefer one of those - eval must NOT be
    // forced to 0. The fix moved the repeat check into the per-move loop: a
    // move that would recreate an earlier position is scored as a draw for
    // THAT move only, compared normally against every other candidate.
    {
        history.clear();
        history.push_back(P0);
        history.push_back(P1);
        history.push_back(P0);

        BB original = P0;
        lookup_table* table = new lookup_table();
        int insertions_before = table->number_of_inserions;

        int eval = play.engine_move(&original, wfh, false, 3, WEIGHTS_OG, table);

        expect(eval>0, "engine_move: White (ahead, to move) must not be forced into a repeat when a better move exists");
        expect(table->number_of_inserions>insertions_before, "engine_move: a real search happened here now, so real subtrees get cached into the TT");
        delete table;
    }

    // --- Genuine upcoming cycle at ply_gap=3: history=[P0,P1,P2,P3],
    // current=P3, Black to move. From P3, Black can play Kbk_to-bk_from,
    // which recreates P0 exactly (White's king is already back home from its
    // own round trip at P1->P2->P3). ply_gap=3 is the smallest gap where this
    // is actually meaningful, unlike the ply_gap=1 case above.
    //
    // Unlike the exact-repeat case above, THIS scenario has the DISADVANTAGED
    // side (Black, down a rook) holding the reversible move - Black has no
    // better option in this bare-king-vs-king-and-rook position, so correctly
    // CHOOSING the draw (eval==0) is the right answer here, not a bug: a draw
    // beats losing. This scenario alone can't distinguish "black rationally
    // took its best available option" from "the old bug forced 0 regardless
    // of alternatives", since both produce the same eval - the case that
    // actually isolates the bug (the ADVANTAGED side facing a reachable cycle)
    // is covered by diagnostics/repetition_forced_draw_bug_test.cpp.
    {
        history.clear();
        history.push_back(P0);
        history.push_back(P1);
        history.push_back(P2);
        history.push_back(P3);

        BB original = P3;
        lookup_table* table = new lookup_table();
        int insertions_before = table->number_of_inserions;

        int eval = play.engine_move(&original, wfh, false, 3, WEIGHTS_OG, table);

        expect(eval==0, "engine_move: Black (behind, to move) should rationally take the only draw available");
        expect(table->number_of_inserions>insertions_before, "engine_move: Black's other (losing) candidate moves are still really searched and cached, even though the draw wins out");
        delete table;
    }

    // --- result(): the actual game-ending threefold check (real FIDE rule,
    // deliberately stricter than minimax()'s search-time heuristic above).
    {
        history.clear();
        history.push_back(P0);
        history.push_back(P1);
        history.push_back(P0); // P0 has now occurred twice
        expect(result(&P0)==0, "result(): twice is not yet a threefold repetition - game must still be on");

        history.push_back(P1);
        history.push_back(P0); // P0 has now occurred three times
        expect(result(&P0)==2, "result(): three occurrences of the same position must end the game as a draw");
    }

    history.clear();
    std::cout << "All repetition detection probe checks passed." << std::endl;
    return 0;
}
