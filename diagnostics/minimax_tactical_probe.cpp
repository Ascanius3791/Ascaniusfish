// OWNERSHIP=Claude
// Hand-checkable correctness cases for minimax_tactical() (ascaniusfish_2.hpp),
// the capture-only tactical search that replaced the old in-tree SEE extension
// heuristic. Covers: a quiet leaf with no table entry, a quiet leaf with a cached
// exact TT entry, a simple one-capture-then-quiet position, a multi-ply recapture
// chain that stops at a losing recapture, and a checkmate leaf.
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

int main()
{
    setup();
    BB* wfh = new BB[4000];

    // (1) Fully quiet position, no table: should fall straight to eval(...,0).
    {
        BB parent;
        FEN_to_BB("4k3/8/8/8/8/8/8/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        PV_Line result = minimax_tactical(&parent, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
        expect(result.current_lenght==0, "(1) quiet leaf should have an empty PV");
        expect(result.eval==0, "(1) two bare kings should evaluate to 0, got " + std::to_string(result.eval));
    }

    // (2) Same quiet position, but with a pre-populated exact TT entry: should
    // prefer the cached eval over calling eval() again.
    {
        BB parent;
        FEN_to_BB("4k3/8/8/8/8/8/8/4K3 w - - 0 1", &parent);
        castling_rights(&parent);

        lookup_table* table = new lookup_table; // ~580MB (TT_EXPONENT_FOR_SIZE) - must be heap-allocated
        TT_entry entry;
        entry.board = parent;
        entry.zobrist_hash = parent.zobrist_hash;
        entry.initialized = true;
        entry.pv_line = PV_Line(12345);
        entry.pv_line.bound_type = 0;
        entry.pv_line.current_lenght = 0;
        table->insert(entry);

        PV_Line result = minimax_tactical(&parent, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, table);
        expect(result.eval==12345, "(2) expected the cached TT eval (12345), got " + std::to_string(result.eval));
    }

    // (3) One undefended capture, nothing further: PV should be exactly the
    // capturing move, landing on a quiet leaf afterwards.
    {
        BB parent;
        FEN_to_BB("4k3/8/8/3n4/8/8/3R4/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        PV_Line result = minimax_tactical(&parent, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
        expect(result.current_lenght==1, "(3) expected a 1-move PV (the capture), got " + std::to_string(result.current_lenght));
        expect(result.moves[0].to==(3+8*4), "(3) expected the PV move to land on d5");
    }

    // (4) Doubled white rooks on the d-file, undefended black knight on d8, black
    // queen on a8 as the only potential recapture. Root move Rd4xNd8 is a good
    // capture for white (SEE=+300: knight falls, queen recaptures the rook, the
    // x-rayed second rook on d1 recaptures the queen). But at the resulting child
    // position, black's only capture (Qxd8, retaking the rook) is a LOSING
    // recapture for black (SEE=-400, since Rd1 still guards d8) and must be
    // declined - the tactical search should stop after white's single capture,
    // not follow through the full (bad-for-black) recapture.
    {
        BB parent;
        FEN_to_BB("q2n3k/8/8/8/3R4/8/8/3RK3 w - - 0 1", &parent);
        castling_rights(&parent);
        PV_Line result = minimax_tactical(&parent, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
        expect(result.current_lenght==1, "(4) expected white's capture then a quiet leaf (PV length 1), got " + std::to_string(result.current_lenght));
        expect(result.moves[0].to==(3+8*7), "(4) expected the PV move to land on d8");
    }

    // (5) Checkmate position with zero legal moves at all: minimax_tactical must
    // return the correct mate score via number_of_new_moves==0, without needing a
    // table or calling eval().
    {
        // Hand-verified checkmate: black king a8, white queen b7 (defended by the
        // white king on c7) - a7/b8 covered by the queen, b7 defended so the king
        // can't capture it either.
        BB parent;
        FEN_to_BB("k7/1QK5/8/8/8/8/8/8 b - - 0 1", &parent);
        castling_rights(&parent);
        PV_Line result = minimax_tactical(&parent, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
        expect(result.current_lenght==0, "(5) checkmate leaf should have an empty PV");
        expect(result.eval==INT_MAX, "(5) black to move and checkmated should score INT_MAX (best for white), got " + std::to_string(result.eval));
    }

    std::cout << "minimax_tactical_probe passed\n";
    return 0;
}
