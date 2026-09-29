// OWNERSHIP=Claude
// Positions whose solution hinges on a check at the last ply of the search
// (#33): a fork, skewer or discovered attack that only pays off after the
// checked side has moved, a mate that follows a forced block, and moves that
// walk into such a check. Each is searched by iterative deepening 1..depth,
// the way "go depth N" does; "want" names the move that must be played,
// "avoid" the move that must not be.
//
// It also checks the property directly: minimax_tactical() of the position
// after each "want" move (the side to move is in check) must search an
// evasion, i.e. return a PV, or a mate score if there is none - never the
// static eval.
//
// main before #33 (c86eb83) solved 1/12: the move column below is what it
// played, and its minimax_tactical() returned the static eval in check for
// every "want" case but #8, where the block is the only legal move and the
// forced-move rule already searched it. #3 and #7 it played by luck, scored
// on the static eval of the position in check.
//    1 e4d2   2 b5d6   3 e4e5   4 h1b1   5 a4a5   6 e4d6
//    7 d1d5   8 e1e8 (ok)   9 e1c1  10 e5d7  11 d8d5  12 d1d4
//
// Build from the repo root:
//   g++ -O3 -mpopcnt -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/check_horizon_test diagnostics/check_horizon_test.cpp
#include "../lib/uci.hpp"

#include <cstdio>
#include <string>

struct Horizon_Case
{
    const char* fen;
    int depth;
    const char* move;
    bool avoid;       // false: `move` must be played, true: it must not be
    const char* what;
};

static const Horizon_Case CASES[] =
{
    {"4k3/8/8/3q4/4N3/8/8/4K3 w - - 0 1",           1, "e4f6", false, "knight fork K+Q"},
    {"r3k3/8/8/1N6/8/8/8/4K3 w - - 0 1",            1, "b5c7", false, "knight fork K+R"},
    {"8/8/3k1r2/8/3PP3/8/8/4K3 w - - 0 1",          1, "e4e5", false, "pawn fork K+R"},
    {"4q3/8/8/4k3/8/8/8/K6R w - - 0 1",             1, "h1e1", false, "rook skewer K+Q"},
    {"8/3q4/2k5/8/P7/8/8/4KB2 w - - 0 1",           1, "f1b5", false, "bishop skewer K+Q"},
    {"4k3/8/q7/8/4N3/8/8/4R1K1 w - - 0 1",          1, "e4c5", false, "discovered check wins Q"},
    {"r5k1/8/8/8/8/8/8/3QK3 w - - 0 1",             1, "d1d5", false, "queen fork K+R"},
    {"6k1/5ppp/8/2b5/8/B7/5PPP/4R1K1 w - - 0 1",    1, "e1e8", false, "back rank: forced block, then mate"},
    {"8/8/1b5k/8/8/8/8/1K2R3 w - - 0 1",            1, "e1e6", false, "rook fork K+B"},
    {"4k3/8/8/4n3/3Q4/8/8/4K3 b - - 0 1",           1, "e5f3", false, "knight fork K+Q, black"},
    {"3q2k1/5ppp/8/3P1N2/8/8/5PPP/6K1 b - - 0 1",   2, "d8d5", true,  "Qxd5?? walks into Ne7+"},
    {"6k1/5ppp/8/8/3p1n2/8/5PPP/3Q2K1 w - - 0 1",   2, "d1d4", true,  "Qxd4?? walks into Ne2+"},
};

static bool is_mate_score(int eval)
{
    return eval<=INT_MIN+max_mating_seq || eval>=INT_MAX-max_mating_seq;
}

int main()
{
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    lookup_table* table = new lookup_table;
    BB* wfh = new BB[UCI_WFH_SIZE];
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable* cycle_table = new CuckooCycleTable;

    int failures = 0, index = 0;
    for(const Horizon_Case& c : CASES)
    {
        index++;
        BB root;
        if(!uci_parse_fen(c.fen, root))
        {
            std::printf("%2d invalid FEN %s\n", index, c.fen);
            return 1;
        }
        table->reset();
        clear_killer_moves();
        path_history[0] = root;

        PV_Line pv;
        for(int d=1; d<=c.depth; d++)
        pv = minimax(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, 0, cycle_table);
        BB child;
        make_move(&root, pv.at(0), &child);
        const std::string played = get_UCI(&root, &child);
        bool solved = c.avoid ? played!=c.move : played==c.move;

        // The property itself, on the position the "want" move leads to.
        std::string horizon = "";
        if(!c.avoid)
        {
            BB checked;
            uci_apply_move(root, c.move, checked);
            PV_Line q = minimax_tactical(&checked, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
            const bool searched = q.current_lenght>0 || is_mate_score(q.eval);
            horizon = searched ? "  qsearch: evasion searched" : "  qsearch: STATIC EVAL IN CHECK";
            solved = solved && searched;
        }
        failures += !solved;
        std::printf("%2d %-4s depth %d  %s %s: played %s, score %s%s  -- %s\n",
                    index, solved ? "ok" : "FAIL", c.depth, c.avoid ? "avoid" : "want", c.move,
                    played.c_str(), uci_score(pv.eval, root.white_move).c_str(), horizon.c_str(), c.what);
    }
    std::printf("check_horizon_test: %d/%d solved\n", (int)(sizeof(CASES)/sizeof(CASES[0]))-failures, (int)(sizeof(CASES)/sizeof(CASES[0])));

    delete table;
    delete[] wfh;
    delete[] path_history;
    delete cycle_table;
    return failures==0 ? 0 : 1;
}
