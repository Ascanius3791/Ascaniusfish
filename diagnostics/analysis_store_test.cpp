// OWNERSHIP=Claude
// Does a position the analysis has already reported on show its eval again?
//
// Issue #24: moving the cursor ends the search, so coming back to a position
// used to mean an empty bar until the engine got going again. gui/session.hpp
// now keeps each position's best result (gui/analysis_store.hpp) and puts it
// back up on arrival. What can go quietly wrong there is showing the wrong
// position's eval — a result filed under the position moved *to* instead of the
// one it was made in, a stale line surviving a new game, a deeper result lost to
// a shallower one — and none of it crashes, so it is checked here rather than by
// watching the page.
//
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -o diagnostics/analysis_store_test diagnostics/analysis_store_test.cpp
//
// Run it from the repo root.
#include "../gui/session.hpp"
#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string& what, const std::string& detail = "")
{
    std::printf("  %-58s %s%s%s\n", what.c_str(), ok ? "ok" : "FAIL",
                ok || detail.empty() ? "" : "  ", ok ? "" : detail.c_str());
    failures += !ok;
}

static void check_eq(long long got, long long want, const std::string& what)
{
    check(got==want, what, "got " + std::to_string(got) + ", wanted " + std::to_string(want));
}

// One iteration as the engine would report it, with a line that is legal from
// the position it is given for.
static Search_Info iteration(int depth, int cp, const std::vector<std::string>& pv)
{
    Search_Info info;
    info.depth = depth;
    info.nodes = 1000*depth;
    info.nps = 500000;
    info.time_ms = 2*depth;
    info.score_kind = "cp";
    info.score_value = std::to_string(cp);
    info.pv = pv;
    return info;
}

static void play(Session& session, const std::string& uci)
{
    std::string error;
    if(!session.play(uci, error))
    check(false, "playing " + uci, error);
}

static void navigate(Session& session, Nav where)
{
    std::string error;
    if(!session.navigate(where, error))
    check(false, "navigating", error);
}

// Stepping away from an analysed position and back again brings its result back,
// marked as a kept one rather than as something the engine just said.
static void test_recall()
{
    std::printf("a position that has been analysed shows its eval again\n");
    Session session;
    session.analysis_on = true;
    session.set_analysis(iteration(9, 35, {"e2e4", "e7e5", "g1f3"}));
    check(session.analysis_valid && !session.analysis_stored, "a live result is not a kept one");

    play(session, "d2d4");
    check(!session.analysis_valid, "the position moved to has nothing to show");

    navigate(session, Nav::BACK);
    check(session.analysis_valid, "back at the analysed position, there is a result");
    check(session.analysis_stored, "and it is marked as a kept one");
    check_eq(session.analysis.depth, 9, "its depth is the depth reached");
    check_eq(std::atoll(session.analysis.score_value.c_str()), 35, "its score is the score found");
    check_eq((long long)session.analysis_san.size(), 3, "its whole line comes back");
    check(session.analysis_san.size()==3 && session.analysis_san[0]=="e4", "in SAN, from this position");
}

// The live search takes over as soon as it is as deep as what is shown, and not
// before: a depth that fell back to 1 on every step would be worse than useless.
static void test_catching_up()
{
    std::printf("a shallower live search does not replace a deeper kept result\n");
    Session session;
    session.analysis_on = true;
    session.set_analysis(iteration(9, 35, {"e2e4"}));
    play(session, "d2d4");
    navigate(session, Nav::BACK);

    session.set_analysis(iteration(3, -10, {"b1c3"}));
    check(session.analysis_stored, "at depth 3 the kept result is still the one shown");
    check_eq(session.analysis.depth, 9, "the depth shown does not go backwards");
    check_eq(session.analysis_live_depth, 3, "how far the search has got is known");

    session.set_analysis(iteration(9, -10, {"b1c3"}));
    check(!session.analysis_stored, "reaching that depth, the search takes over");
    check_eq(std::atoll(session.analysis.score_value.c_str()), -10, "with its own score");

    session.set_analysis(iteration(11, -20, {"b1c3"}));
    play(session, "d2d4");
    navigate(session, Nav::BACK);
    check_eq(session.analysis.depth, 11, "and the deeper result is what is kept");
}

// The entries are positions, not moves: the same position reached another way
// has the same analysis, and a position never looked at has none.
static void test_positions_not_nodes()
{
    std::printf("entries belong to positions\n");
    Session session;
    session.analysis_on = true;
    session.set_analysis(iteration(8, 20, {"e2e4"}));

    // A transposition: four moves that put the start position back on the board.
    play(session, "g1f3");
    play(session, "g8f6");
    play(session, "f3g1");
    play(session, "f6g8");
    check(session.analysis_valid && session.analysis_stored, "a transposition finds the entry");
    check_eq(session.analysis.depth, 8, "with the depth it was made at");

    play(session, "d2d4");
    check(!session.analysis_valid, "a position never analysed shows nothing");
}

// A new game is a new store. Anything else would show one game's eval in
// another's position.
static void test_new_game_clears()
{
    std::printf("a new game, a FEN or a loaded PGN clears the store\n");
    const std::string KIWIPETE =
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

    Session session;
    session.analysis_on = true;
    session.set_analysis(iteration(8, 20, {"e2e4"}));
    session.reset();
    check(!session.analysis_valid, "after a reset the start position has nothing");

    session.set_analysis(iteration(8, 20, {"e2e4"}));
    std::string error;
    check(session.set_fen(KIWIPETE, error), "a FEN loads", error);
    check(!session.analysis_valid, "and clears what the last game found");

    session.set_analysis(iteration(7, 90, {"e2a6"}));
    check(session.load_pgn("1. e4 e5 2. Nf3", error), "a PGN loads", error);
    check(!session.analysis_valid, "and clears it too");
    navigate(session, Nav::START);
    check(!session.analysis_valid, "its start position included");
}

// An entry is only ever handed back for the position it was made in — the store
// is keyed by position, so the way the cursor got there cannot matter.
static void test_nothing_stale()
{
    std::printf("no position ever shows another one's result\n");
    Session session;
    session.analysis_on = true;
    session.set_analysis(iteration(8, 20, {"e2e4", "e7e5"}));
    play(session, "e2e4");
    session.set_analysis(iteration(8, -15, {"e7e5", "g1f3"}));
    play(session, "e7e5");
    session.set_analysis(iteration(8, 25, {"g1f3"}));

    navigate(session, Nav::BACK);
    check_eq(std::atoll(session.analysis.score_value.c_str()), -15, "after 1.e4, the eval of after 1.e4");
    navigate(session, Nav::BACK);
    check_eq(std::atoll(session.analysis.score_value.c_str()), 20, "at the start, the start's");
    navigate(session, Nav::END);
    check_eq(std::atoll(session.analysis.score_value.c_str()), 25, "at the end, the end's");

    // A move on into a position nobody has analysed: an entry of its own, empty.
    play(session, "c2c4");
    check(!session.analysis_valid, "a position further on has nothing to show");
}

int main()
{
    // Without these, sliding attacks are garbage and in_check() quietly misses
    // checks — every tool that touches movegen starts here.
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::printf("gui/analysis_store.hpp: a known position's eval, before the search\n\n");
    test_recall();
    test_catching_up();
    test_positions_not_nodes();
    test_new_game_clears();
    test_nothing_stale();

    std::printf("\n%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
