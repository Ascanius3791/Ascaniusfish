// OWNERSHIP=Claude
// Which search the eval bar and the engine-line box are showing.
//
// Issue #25: the bar and the line box are in Play and Watch too, and there the
// number belongs to the move you are looking at rather than to the position —
// step back onto a move the engine played and you get the search that chose it,
// step onto one of your own and you get nothing. What can go quietly wrong is
// all of it silent: a score shown from the mover's view instead of white's (the
// bar would jump a board width every move), a line kept with one move drawn
// under another, the newest number left up at a position it says nothing about.
// None of that crashes, so it is checked here rather than by watching the page.
//
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -o diagnostics/eval_view_test diagnostics/eval_view_test.cpp
//
// Run it from the repo root. No engine process is started: the searches here are
// handed to the session the way gui_server.cpp hands it the real ones.
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

static void check_line(const Eval_View& view, const std::string& want, const std::string& what)
{
    std::string got;
    for(const std::string& san : view.san)
    got += (got.empty() ? "" : " ") + san;
    check(got==want, what, "got \"" + got + "\", wanted \"" + want + "\"");
}

// One iteration as the engine reports it: a score from the mover's view and a
// line that is legal from the position searched.
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

// The engine's move, as collect_search() makes it: the search is live while it
// runs, then the move is played and the search is filed with it.
static void engine_plays(Session& session, const Search_Info& info, const std::string& uci)
{
    bool white_moved = session.white_to_move();
    session.live = info;
    session.live_valid = true;
    session.searching = Search_Kind::NONE;
    session.live_valid = false;
    play(session, uci);
    session.annotate_last(info, white_moved);
}

// A search running now is about the position on the board, and the score it
// reaches is the one the move it ends in will carry: the bar must not jump when
// the move is played.
static void test_live_then_move()
{
    Session session("live");
    session.mode = Mode::PLAY;
    play(session, "e2e4");          // the human's move: nothing searched it

    check(session.eval_view().from==Eval_From::NONE, "a move of your own shows nothing");

    // Black thinking, and liking its position by 0.8: white's view is -80.
    session.searching = Search_Kind::PLAY;
    session.live = iteration(7, 80, {"e7e5", "g1f3", "b8c6"});
    session.live_valid = true;
    Eval_View live = session.eval_view();
    check(live.from==Eval_From::LIVE, "a running search is what is shown");
    check_eq(live.score_value, -80, "a black search's score is shown from white's view");
    check_eq(live.depth, 7, "the depth it has reached");
    check_line(live, "e5 Nf3 Nc6", "its line starts at the position on the board");

    engine_plays(session, iteration(7, 80, {"e7e5", "g1f3", "b8c6"}), "e7e5");
    Eval_View played = session.eval_view();
    check(played.from==Eval_From::MOVE, "after the move, the move's own search");
    check_eq(played.score_value, -80, "with the same number the search ended on");
    check_eq(played.depth, 7, "and the same depth");
    check_line(played, "Nf3 Nc6", "its line goes on from the move that was played");
}

// Stepping around a game: every engine move carries its own search, and a
// position no engine ever searched carries nothing.
static void test_stepping_back()
{
    Session session("steps");
    session.mode = Mode::PLAY;
    play(session, "e2e4");
    engine_plays(session, iteration(6, 40, {"c7c5", "g1f3"}), "c7c5");
    play(session, "g1f3");
    engine_plays(session, iteration(8, 120, {"d7d6", "d2d4"}), "d7d6");

    check_eq(session.eval_view().depth, 8, "the newest move shows its own depth");
    check_eq(session.eval_view().score_value, -120, "and its own score");

    navigate(session, Nav::BACK);   // 2. Nf3, a move of your own
    check(session.eval_view().from==Eval_From::NONE, "a move nothing searched shows nothing");

    navigate(session, Nav::BACK);   // 1... c5, the engine's
    Eval_View earlier = session.eval_view();
    check(earlier.from==Eval_From::MOVE, "an earlier engine move shows its own search");
    check_eq(earlier.depth, 6, "the depth that move reached, not the newest one");
    check_eq(earlier.score_value, -40, "the score that move was played on");
    check_line(earlier, "Nf3", "and the line that search found from there");

    navigate(session, Nav::START);
    check(session.eval_view().from==Eval_From::NONE, "the start of the game shows nothing");
}

// A white move's score keeps its sign, a black move's flips: both are white's
// view once they are in the tree, which is the one thing the bar depends on.
static void test_white_view()
{
    Session session("sides");
    session.mode = Mode::WATCH;
    engine_plays(session, iteration(5, 30, {"d2d4"}), "d2d4");
    check_eq(session.eval_view().score_value, 30, "a white search of +0.30 stays +0.30");
    engine_plays(session, iteration(5, 30, {"d7d5"}), "d7d5");
    check_eq(session.eval_view().score_value, -30, "a black search of +0.30 becomes -0.30");
}

// The line kept with a move was found in the position before it, so the move
// itself is its first entry. A line that does not begin with the move played is
// some other move's, and showing it under this one would be a lie.
static void test_line_belongs_to_its_move()
{
    Session session("lines");
    session.mode = Mode::PLAY;
    play(session, "e2e4");
    engine_plays(session, iteration(6, 20, {"g8f6", "b1c3"}), "e7e5");   // pv of a move not played
    check_line(session.eval_view(), "", "a line that is not this move's is not shown");
    check_eq(session.eval_view().depth, 6, "its score and depth still are");
}

// In Analyse mode the subject is the position, not the move: a result kept for
// it (#24) is the better answer and wins over whatever a move carries.
static void test_analyse_prefers_the_position()
{
    Session session("analyse");
    session.mode = Mode::WATCH;
    engine_plays(session, iteration(5, 50, {"e2e4", "e7e5"}), "e2e4");
    check(session.eval_view().from==Eval_From::MOVE, "in Watch the move's search is shown");

    // An analysis of the position after that move, deeper than the move's own.
    session.mode = Mode::ANALYSE;
    session.set_analysis(iteration(12, -200, {"e7e5", "g1f3"}));
    check(session.eval_view().from==Eval_From::LIVE, "the analysis running now wins");
    check_eq(session.eval_view().score_value, 200, "shown from white's view");

    // Away and back: the analysis is kept for the position it was made in.
    play(session, "e7e5");
    navigate(session, Nav::BACK);
    Eval_View back = session.eval_view();
    check(back.from==Eval_From::STORED, "coming back shows the kept analysis");
    check_eq(back.depth, 12, "at the depth it reached, not the move's");
    check_line(back, "e5 Nf3", "with the line it found");

    // The same position in Play mode: there the move you are on is the subject.
    session.mode = Mode::PLAY;
    check(session.eval_view().from==Eval_From::MOVE, "in Play the move's own search wins");
    check_eq(session.eval_view().depth, 5, "the depth that move was played at");
}

// A position the tables know is answered by the tables, above every search
// (#40), from white's view whoever is to move. Needs the tables: the directory
// is argv[1], or ~/syzygy-nr; without them this is skipped, not failed.
static void test_tablebase(const std::string& dir)
{
    tablebase_setup().load(dir);
    if(!tablebase_setup().available())
    {
        std::printf("  (no tables in \"%s\": tablebase checks skipped)\n", dir.c_str());
        return;
    }
    std::string error;
    Session session("tb");
    check(session.set_fen("8/8/8/4k3/8/8/4K3/R7 w - - 0 1", error), "KRvK loads", error);
    session.set_analysis(iteration(9, -50, {"e5d5"}));   // a search that disagrees
    Eval_View v = session.eval_view();
    check(v.from==Eval_From::TB, "a 3-piece position is the tables', not the search's");
    check_eq(v.score_value, 2, "white to move and winning: +2 in white's view");
    check(v.tb_dtz>0, "with its DTZ");

    Session black("tb-black");
    check(black.set_fen("8/8/8/4k3/8/8/4K3/R7 b - - 0 1", error), "the same with black to move", error);
    check_eq(black.eval_view().score_value, 2, "still white's win from white's view");
    check(black.set_fen("8/8/8/4k3/8/8/4K3/r7 w - - 0 1", error), "and black's rook", error);
    check_eq(black.eval_view().score_value, -2, "a black win is -2");

    std::string json = session.state_json();
    check(json.find("\"tbMoves\":[{")!=std::string::npos, "Analyse lists the legal moves with results");
    check(json.find("\"source\":\"tb\"")!=std::string::npos, "and eval says its source is tb");

    session.tb_limit = 3;
    check(session.set_fen("8/8/8/4k3/8/8/3PK3/8 w - - 0 1", error), "KPvK loads", error);
    check(session.eval_view().from==Eval_From::TB, "3 pieces at limit 3");
    check(session.set_fen("8/8/8/4k3/8/8/3PK3/7R w - - 0 1", error), "KRPvK loads", error);
    check(session.eval_view().from!=Eval_From::TB, "4 pieces are over limit 3");
    session.tb_limit = 5;
    check(session.eval_view().from==Eval_From::TB, "and inside limit 5");

    session.tb_on = false;
    check(session.eval_view().from!=Eval_From::TB, "the switch off hides them");
    check(session.engine_options()[0].second=="<empty>", "and takes the path from the engines");
    session.tb_on = true;
    check(session.engine_options()[0].second==dir, "on, the engines are given the path");
    check(session.engine_options()[1].second=="5", "with the piece limit");
}

int main(int argc, char** argv)
{
    // Without these, sliding attacks are garbage and in_check() quietly misses
    // checks — every tool that touches movegen starts here.
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::printf("gui/session.hpp: which search the eval bar and the line box show\n\n");
    test_live_then_move();
    test_stepping_back();
    test_white_view();
    test_line_belongs_to_its_move();
    test_analyse_prefers_the_position();
    const char* home = std::getenv("HOME");
    test_tablebase(argc>1 ? argv[1] : std::string(home ? home : "") + "/syzygy-nr");

    std::printf("\n%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
