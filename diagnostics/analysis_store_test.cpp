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
    std::printf("a live search is shown at once, a deeper kept result stays kept\n");
    Session session;
    session.analysis_on = true;
    session.set_analysis(iteration(9, 35, {"e2e4"}));
    play(session, "d2d4");
    navigate(session, Nav::BACK);
    check(session.analysis_stored && session.analysis.depth==9, "coming back, the kept result shows");

    session.set_analysis(iteration(3, -10, {"b1c3"}));
    check(!session.analysis_stored, "the search's depth 3 replaces it on the page (#100: the rebuild shows)");
    check_eq(std::atoll(session.analysis.score_value.c_str()), -10, "with its own score");
    check_eq(session.analysis_live_depth, 3, "how far the search has got is known");

    play(session, "d2d4");
    navigate(session, Nav::BACK);
    check_eq(session.analysis.depth, 9, "the store still keeps the deeper result");

    session.set_analysis(iteration(11, -20, {"b1c3"}));
    play(session, "d2d4");
    navigate(session, Nav::BACK);
    check_eq(session.analysis.depth, 11, "and the deeper result is what is kept");
}

// A mate is a proof (#100): it beats a kept score however deep, a deeper score
// does not beat a kept mate, and at equal depth the fresh result is the one kept.
static void test_mates()
{
    std::printf("a mate beats a kept score however deep\n");
    Session session;
    session.analysis_on = true;
    session.set_analysis(iteration(18, 2716, {"e2e4"}));
    play(session, "d2d4");
    navigate(session, Nav::BACK);

    Search_Info mate = iteration(3, 0, {"g1f3"});
    mate.score_kind = "mate";
    mate.score_value = "10";
    session.set_analysis(mate);
    check(!session.analysis_stored && session.analysis.score_kind=="mate", "a live mate at depth 3 replaces a kept score at depth 18");
    play(session, "d2d4");
    navigate(session, Nav::BACK);
    check(session.analysis_stored && session.analysis.score_kind=="mate", "and the mate is what is kept");

    session.set_analysis(iteration(20, 2800, {"e2e4"}));
    check(session.analysis_stored && session.analysis.score_kind=="mate", "a live score at depth 20 does not replace a kept mate");

    Analysis_Store store;
    const Position_Key key = session.analysis_key;
    store.put(key, iteration(9, 35, {"e2e4"}));
    store.put(key, iteration(9, -10, {"b1c3"}));
    Search_Info got;
    check(store.get(key, got) && got.score_value=="-10", "a fresh result of equal depth replaces the kept one");
}

// The engine's "route" lines (#100): another route in the tree to a position
// on the cursor's path, only with the Transpositions switch on.
static void test_routes()
{
    std::printf("the tree's other routes to the path, for the engine's refresh\n");
    Session session;
    for(const char* m : {"g1f3", "g8f6", "b1c3", "b8c6"})
    play(session, m);
    navigate(session, Nav::START);
    for(const char* m : {"b1c3", "b8c6", "g1f3", "g8f6", "e2e4"})
    play(session, m);
    check(session.transposition_routes().empty(), "switched off, none");
    session.routes_on = true;
    const std::vector<std::string> routes = session.transposition_routes();
    check(routes.size()==1 && routes[0]=="route g1f3 g8f6 b1c3 b8c6", "on, the other move order to the path's position",
          routes.empty() ? "none" : routes[0]);
}

// "Save to root" (#101) puts an exact entry in the root's TT, and an exact mate
// there is a proof: only a mate the engine confirmed goes in as one (#100).
static void test_corr_save_mates()
{
    std::printf("a save is a mate only if the engine confirmed it\n");
    Session session;
    session.mode = Mode::CORRESPONDENCE;
    session.analysis_on = true;
    Search_Info mate = iteration(7, 0, {"e2e4"});
    mate.score_kind = "mate";
    mate.score_value = "4";
    session.set_analysis(mate);
    std::string error;
    check(session.corr_save(error), "an unconfirmed mate saves", error);
    check(!session.corr_saves.empty() && session.corr_saves.back().score_kind=="cp"
          && session.corr_saves.back().score_value==TB_WIN_CP-1, "as the largest score short of the tables'");

    mate.mate_confirmed = true;
    mate.depth = 8;
    session.set_analysis(mate);
    check(session.corr_save(error), "a confirmed mate saves", error);
    check(session.corr_saves.size()==1 && session.corr_saves.back().score_kind=="mate"
          && session.corr_saves.back().score_value==4, "as the mate, replacing the first save");
}

// The generation is the mark (#100): a save gets it, the list keeps a save by
// the engine's rule, a Return after new saves starts the next generation and
// queues the Searcher's ttdemote and saves, a Return without any does nothing,
// and an older save above a newer one is warned about.
static void test_corr_generations()
{
    std::printf("saves are marked with the generation\n");
    Session session;
    session.mode = Mode::CORRESPONDENCE;
    session.analysis_on = true;
    std::string error;
    auto save_here = [&](int depth, int cp) {
        session.set_analysis(iteration(depth, cp, {session.white_to_move() ? "a2a3" : "a7a6"}));
        return session.corr_save(error);
    };
    play(session, "e2e4");
    check(save_here(20, 30), "a save at depth 20", error);
    check_eq(session.corr_saves.back().mark, 1, "generation 1 marks 1");
    check(session.corr_prelude.size()==2 && session.corr_prelude[0]=="ttdemote", "the root engine demotes first");
    check(save_here(15, 40), "a shallower save, same generation", error);
    check_eq(session.corr_saves.back().depth, 20, "the list keeps depth 20");
    check(!session.corr_warning.empty(), "and says so", session.corr_warning);

    session.corr_return();
    check_eq(session.corr_generation, 2, "Return after a save: generation 2");
    check(session.corr_searcher_prelude.size()==2 && session.corr_searcher_prelude[0]=="ttdemote",
          "the Searcher gets ttdemote and the save");
    session.corr_searcher_prelude.clear();
    session.corr_return();
    check(session.corr_generation==2 && session.corr_searcher_prelude.empty(), "Return without saves: nothing");

    play(session, "e2e4");
    play(session, "e7e5");
    check(save_here(10, 5), "a newer save below the older one", error);
    check(session.corr_warning.find("older save")!=std::string::npos, "warns of the older save above", session.corr_warning);
    navigate(session, Nav::BACK);
    check(save_here(12, 50), "the older position again, generation 2, shallower", error);
    check(session.corr_saves[0].depth==12 && session.corr_saves[0].mark==2, "taken: a newer mark at any depth");
    check(session.corr_warning.find("over the held depth")!=std::string::npos, "with a warning", session.corr_warning);

    check(session.corr_update_start(error), "full update starts", error);
    check(session.corr_generation==3 && session.corr_update_total==2 && session.corr_update_node==session.corr_saves[1].node,
          "generation 3, two steps, the deepest first");
    check_eq(session.corr_update_limit(), 10, "on the board, to its depth");
    navigate(session, Nav::BACK);
    check(!session.corr_updating && session.corr_update_note.find("stopped")!=std::string::npos, "moving the board stops it", session.corr_update_note);
}

// A save keeps its whole line, the board on its position (by any route) finds
// it, "Ask the root" queues a ttprobe and reads its answer, and the root's
// line is entered from the root wherever the cursor is (#100).
static void test_corr_lines()
{
    std::printf("a save's line, the board on a save, the root's probe and line\n");
    Session session;
    session.mode = Mode::CORRESPONDENCE;
    session.analysis_on = true;
    std::string error;
    play(session, "g1f3");
    play(session, "g8f6");
    play(session, "b1c3");
    session.set_analysis(iteration(14, 25, {"b8c6", "e2e4", "e7e5"}));
    check(session.corr_save(error), "a save with a three-move line", error);
    check(session.corr_saves.back().line_uci.size()==3 && session.corr_saves.back().line_san[1]=="e4",
          "keeps the whole line, both ways");
    check(session.corr_prelude.back().find(" move b8c6 fen ")!=std::string::npos, "ttmark gets its first move", session.corr_prelude.back());

    navigate(session, Nav::START);
    check(session.corr_save_on_board()==nullptr, "the start is no save");
    play(session, "b1c3");
    play(session, "g8f6");
    play(session, "g1f3");
    const Corr_Save* here = session.corr_save_on_board();
    check(here && here->depth==14, "the same position by another route is the save");

    session.corr_prelude.clear();
    session.corr_probe_ask();
    check(session.corr_prelude.size()==1 && session.corr_prelude[0].compare(0, 12, "ttprobe fen ")==0,
          "Ask the root queues a ttprobe", session.corr_prelude.empty() ? "" : session.corr_prelude[0]);
    check(session.corr_restart, "and restarts the root");
    const std::vector<std::string> fresh = session.corr_root_prelude(true);
    check(fresh.size()==2 && fresh.back()==session.corr_prelude[0], "a new game gets every save and the probe");
    session.corr_note("ttprobe mark 1 depth 17 bound 0 score cp -30 pv b8c6 e2e4");
    check(!session.corr_probe.pending && session.corr_probe.found && session.corr_probe.depth==17
          && session.corr_probe.score_value==-30 && session.corr_probe.pv.size()==2, "the answer is read");
    check(session.corr_notes.empty(), "and kept out of the notes");
    session.corr_probe_ask();
    session.corr_note("ttprobe none");
    check(session.corr_probe.asked && !session.corr_probe.pending && !session.corr_probe.found, "a miss is found=false");
    const std::vector<std::string> line = {"e2e4", "e7e5"};
    check(session.enter_line("e2e4 e7e5", error, session.corr_root_id()), "the root's line from the root", error);
    check(session.moves()==line, "the board is the root (the start) plus the line");
    check(!session.enter_line("e2e4 e2e4", error, session.corr_root_id()), "an illegal line is refused");
    check(session.moves()==line, "and leaves the board alone");
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

// MultiPV (#69): a kept result stands in for the live search only if it has as
// many lines too, and one with more lines than are wanted now shows K of them.
static void test_lines()
{
    std::printf("a kept result's lines: never fewer than asked for, never more shown\n");
    Session session;
    session.analysis_on = true;
    session.set_analysis(iteration(12, 35, {"e2e4"}));   // deep, one line
    play(session, "d2d4");
    navigate(session, Nav::BACK);

    session.analysis_lines = 3;
    Search_Info three = iteration(4, 30, {"e2e4"});
    three.more = { {"cp", "20", {"d2d4", "d7d5"}}, {"cp", "-15", {"g1f3"}} };
    session.set_analysis(three);
    check(!session.analysis_stored, "three shallow lines replace one deep line when three are wanted");
    Eval_View v = session.eval_view();
    check_eq((long long)v.more.size(), 2, "lines 2 and 3 are on show");
    check(v.more.size()==2 && v.more[0].san.size()==2 && v.more[0].san[0]=="d4", "replayed in SAN from here");
    check(v.more.size()==2 && v.more[1].score_value==-15, "each with its own score, white's view");

    play(session, "d2d4");
    navigate(session, Nav::BACK);
    check(session.analysis_stored && session.analysis.more.size()==2, "the three lines are what is kept");

    session.analysis_lines = 2;
    check_eq((long long)session.eval_view().more.size(), 1, "with K lowered to 2, the kept three show two");
    session.analysis_lines = 1;
    check_eq((long long)session.eval_view().more.size(), 0, "and at K=1, one");
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
    test_lines();
    test_mates();
    test_routes();
    test_corr_save_mates();
    test_corr_generations();
    test_corr_lines();

    std::printf("\n%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
