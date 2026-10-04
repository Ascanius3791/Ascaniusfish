// OWNERSHIP=Claude
// Does the PTT (#79, gui/ptt.hpp) keep long searches across a restart, and only
// the ones it should?
//
// What can go quietly wrong: an entry that does not survive the file round trip,
// a shallower or older-eval result overwriting a better one, a second GUI's
// appends lost to a compaction, an older eval's score trusted as current, a
// score that came from the game's history kept as the position's own, or a
// stored move played where this game's history (a repetition, the 50-move
// clock) makes it something else. None of it crashes, so it is checked here.
//
//   g++ -O3 -mpopcnt -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -o diagnostics/ptt_test diagnostics/ptt_test.cpp
//
// Run it from the repo root. It writes only to a temporary file.
#include "../gui/session.hpp"
#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string& what, const std::string& detail = "")
{
    std::printf("  %-62s %s%s%s\n", what.c_str(), ok ? "ok" : "FAIL",
                ok || detail.empty() ? "" : "  ", ok ? "" : detail.c_str());
    failures += !ok;
}

static void check_eq(long long got, long long want, const std::string& what)
{
    check(got==want, what, "got " + std::to_string(got) + ", wanted " + std::to_string(want));
}

static const char* const START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// One iteration as the engine reports it, found with eval `version`, net `nne` and weight set `weights`.
static Search_Info iteration(int depth, int cp, const std::vector<std::string>& pv, long long ms = 6000,
                             int version = 1, const std::string& nne = "abc", int weights = 1)
{
    Search_Info info;
    info.depth = depth;
    info.nodes = 1000*depth;
    info.nps = 500000;
    info.time_ms = ms;
    info.score_kind = "cp";
    info.score_value = std::to_string(cp);
    info.pv = pv;
    info.prov.eval_version = version;
    info.prov.nne = nne;
    info.prov.weights = weights;
    info.prov.syzygy = 5;
    info.prov.commit = "abc1234";
    return info;
}

static PTT_Entry entry(const std::string& fen, const Search_Info& info)
{
    PTT_Entry e;
    e.fen = fen;
    e.info = info;
    return e;
}

static PTT_Key key(const std::string& fen)
{
    PTT_Key k{};
    PTT::key_of(fen, k);
    return k;
}

static int lines_in(const std::string& path)
{
    std::ifstream in(path);
    std::string line;
    int n = 0;
    while(std::getline(in, line))
    n++;
    return n;
}

static void play(Session& session, const std::string& uci)
{
    std::string error;
    if(!session.play(uci, error))
    check(false, "playing " + uci, error);
}

static void test_round_trip(const std::string& path)
{
    std::printf("an entry survives the file\n");
    std::remove(path.c_str());
    PTT a;
    std::string error;
    check(a.open(path, error), "the file is created", error);
    check(a.put(key(START), entry(START, iteration(20, 31, {"e2e4", "e7e5", "g1f3"}))), "an entry is kept");
    PTT b;
    b.open(path, error);
    const PTT_Entry* e = b.get(key(START));
    check(e!=nullptr, "a new store on the same file finds it");
    if(!e)
    return;
    check(e->fen==START, "with the board, clocks included");
    check_eq(e->info.depth, 20, "its depth");
    check(e->info.score_kind=="cp" && e->info.score_value=="31", "its score");
    check(e->info.pv.size()==3 && e->info.pv[2]=="g1f3", "its line");
    check_eq(e->info.nodes, 20000, "its nodes");
    check_eq(e->info.time_ms, 6000, "its time");
    check(e->info.prov.eval_version==1 && e->info.prov.nne=="abc" && e->info.prov.syzygy==5
          && e->info.prov.commit=="abc1234", "and how it was found");
    check(e->stored_at>0, "and when");
}

static void test_better(const std::string& path)
{
    std::printf("deeper wins within an eval, a newer eval beats an older one\n");
    std::remove(path.c_str());
    PTT ptt;
    std::string error;
    ptt.open(path, error);
    const PTT_Key k = key(START);
    ptt.put(k, entry(START, iteration(20, 31, {"e2e4"})));
    check(!ptt.put(k, entry(START, iteration(18, 10, {"d2d4"}))), "a shallower search is not kept");
    check(!ptt.put(k, entry(START, iteration(20, 10, {"d2d4"}))), "nor an equal one");
    check(ptt.put(k, entry(START, iteration(22, 25, {"e2e4"}))), "a deeper one is");
    check(!ptt.put(k, entry(START, iteration(30, 0, {"c2c4"}, 6000, 0))), "an older eval never replaces a newer one");
    check(ptt.put(k, entry(START, iteration(10, 5, {"g1f3"}, 6000, 2))), "a newer eval replaces even a deeper one");
    check(ptt.put(k, entry(START, iteration(9, 5, {"g1f3"}, 6000, 2, "def"))), "another net of the same version is newer");
    check(ptt.put(k, entry(START, iteration(8, 5, {"g1f3"}, 6000, 2, "def", 3))), "and so is another weight set (#85)");
    check_eq(ptt.get(k)->info.depth, 8, "what is kept is the last of those");
    PTT again;
    again.open(path, error);
    check_eq(again.get(k)->info.depth, 8, "and reading the file gives the same answer");
    check_eq(again.get(k)->info.prov.weights, 3, "with its weight set");
}

static void test_two_guis(const std::string& path)
{
    std::printf("two GUIs share the file\n");
    std::remove(path.c_str());
    PTT a, b;
    std::string error;
    a.open(path, error);
    b.open(path, error);
    const std::string after_e4 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
    a.put(key(after_e4), entry(after_e4, iteration(15, -20, {"c7c5"})));
    check(b.get(key(after_e4))!=nullptr, "what one appends the other finds");

    // Enough superseded lines that opening the file compacts it.
    for(int d=1; d<=150; d++)
    a.put(key(START), entry(START, iteration(d, d, {"e2e4"})));
    check_eq(lines_in(path), 151, "every improvement is a line");
    PTT c;
    c.open(path, error);
    check_eq(lines_in(path), 2, "opening it again drops the replaced lines");
    a.put(key(START), entry(START, iteration(151, 1, {"e2e4"})));
    check_eq(lines_in(path), 3, "an append after the compaction lands in the new file");
    check(b.get(key(START)) && b.get(key(START))->info.depth==151, "and a GUI that read the old file sees it");

    std::ofstream torn(path, std::ios::app);
    torn << "fen=garbage\tdepth=x\n";
    torn.close();
    check(c.get(key(START))!=nullptr, "a line that is not an entry is skipped");
}

static void test_session(const std::string& path)
{
    std::printf("the session keeps long searches and shows them after a restart\n");
    std::remove(path.c_str());
    std::string error;
    persistent_tt().open(path, error);
    engine_build().eval_version = 1;
    engine_build().net_hash = "abc";
    {
        Session s;
        s.set_analysis(iteration(12, 40, {"e2e4", "e7e5"}, 4000));
        check(persistent_tt().get(key(s.fen()))==nullptr, "a search under 5 s is not kept");
        Search_Info multi = iteration(14, 40, {"e2e4", "e7e5"});
        multi.more.push_back({"cp", "30", {"d2d4"}});
        s.set_analysis(multi);
        check(persistent_tt().get(key(s.fen()))==nullptr, "nor one with more than one line");
        Search_Info no_prov = iteration(14, 40, {"e2e4"});
        no_prov.prov = Search_Provenance();
        s.set_analysis(no_prov);
        check(persistent_tt().get(key(s.fen()))==nullptr, "nor one whose engine did not say how it scores");
        s.set_analysis(iteration(16, 40, {"e2e4", "e7e5"}));
        check(persistent_tt().get(key(s.fen()))!=nullptr, "a 5 s search at MultiPV 1 is");
    }
    persistent_tt().open(path, error);   // a server restart
    {
        Session s;
        check(s.analysis_valid && s.analysis_stored && s.analysis_ptt, "a new session shows it at once, no engine");
        check(!s.analysis_older, "as current");
        check_eq(s.eval_view().depth, 16, "with its depth");
        check(s.eval_view().ptt && s.eval_view().from==Eval_From::STORED, "marked as kept");
        check(s.ptt_hint()==(PTT_SEED ? "hint depth 16 pv e2e4 e7e5" : ""), "its line is the next search's hint");
        Search_Info out;
        Go_Limits depth12;
        depth12.depth = 12;
        Go_Limits depth20;
        depth20.depth = 20;
        check(s.ptt_move_now(depth12, out) && out.pv[0]=="e2e4", "deep enough for a depth-12 move: played at once");
        check(!s.ptt_move_now(depth20, out), "not for a depth-20 one");
        Go_Limits clock;
        clock.depth = 0;
        clock.wtime_ms = clock.btime_ms = 60000;
        clock.winc_ms = clock.binc_ms = 1000;
        check(s.ptt_move_now(clock, out), "a 6 s search is enough for a 1+1 move");
        clock.wtime_ms = 3600000;
        check(!s.ptt_move_now(clock, out), "not for a move of a one-hour game");
        s.set_analysis(iteration(10, 0, {"d2d4"}));
        check(s.analysis_stored && s.eval_view().depth==16, "a shallower live search does not replace it");
    }
    engine_build().eval_version = 2;   // the eval changed since
    {
        Session s;
        check(s.analysis_valid && s.analysis_older, "after an eval change it is shown as older");
        check(s.eval_view().older, "and the page is told so");
        check(s.ptt_hint().empty(), "it seeds no search");
        Search_Info out;
        Go_Limits depth1;
        depth1.depth = 1;
        check(!s.ptt_move_now(depth1, out), "and plays no move");
        s.set_analysis(iteration(3, 5, {"d2d4"}, 100, 2));
        check(!s.analysis_stored && s.analysis.depth==3, "the first live result replaces it");
    }
    engine_build().eval_version = 1;
    engine_build().net_hash = "other";
    {
        Session s;
        check(s.analysis_older, "a different net is another eval too");
    }
    engine_build().net_hash = "abc";
    engine_build().weights = 2;
    {
        Session s;
        check(s.analysis_older, "and so is a different weight set");
    }
    engine_build().weights = 1;
}

static void test_history(const std::string& path)
{
    std::printf("a score the game's history may be in is not kept\n");
    std::remove(path.c_str());
    std::string error;
    persistent_tt().open(path, error);
    Session s;
    play(s, "g1f3");
    play(s, "g8f6");
    play(s, "f3g1");
    play(s, "f6g8");   // the start position again
    s.set_analysis(iteration(16, 0, {"g1f3", "g8f6"}));
    check(persistent_tt().get(key(s.fen()))==nullptr, "a draw on a position seen before is not kept");
    s.set_analysis(iteration(17, 20, {"e2e4"}));
    check(persistent_tt().get(key(s.fen()))==nullptr, "nor any score there");
    play(s, "b1c3");   // the start position is still in the history, twice
    s.set_analysis(iteration(16, 20, {"e7e5"}));
    check(persistent_tt().get(key(s.fen()))==nullptr, "nor one move on, with the repetition behind");
    play(s, "e7e5");   // a pawn move: the history is gone
    s.set_analysis(iteration(16, 20, {"g1f3"}));
    check(persistent_tt().get(key(s.fen()))!=nullptr, "after a pawn move it is kept again");

    Session t;   // 1.Nc3 Nc6 2.Nb1: the line's Nb8 puts the start position back
    play(t, "b1c3");
    play(t, "b8c6");
    play(t, "c3b1");
    t.set_analysis(iteration(16, 0, {"c6b8", "b1c3"}));
    check(persistent_tt().get(key(t.fen()))==nullptr, "a draw whose line runs into an earlier position is not");
    t.set_analysis(iteration(16, 30, {"c6b8", "b1c3"}));
    check(persistent_tt().get(key(t.fen()))==nullptr, "nor any score whose line does");

    Session u;   // 1.Nc3: Nf6 and Nf3 meet nothing this game has had
    play(u, "b1c3");
    u.set_analysis(iteration(16, 0, {"g8f6", "g1f3"}));
    check(persistent_tt().get(key(u.fen()))!=nullptr, "a draw whose line meets no earlier position is kept");
}

static void test_use(const std::string& path)
{
    std::printf("a kept move is only played where this game's history agrees\n");
    std::remove(path.c_str());
    std::string error;
    persistent_tt().open(path, error);
    engine_build().eval_version = 1;
    engine_build().net_hash = "abc";
    Go_Limits depth8;
    depth8.depth = 8;
    Search_Info out;

    // Y: both sides' knights out on f3/c3 and f6/c6, white to move, clock 4.
    const char* const Y = "r1bqkb1r/pppppppp/2n2n2/8/8/2N2N2/PPPPPPPP/R1BQKB1R w KQkq - 4 3";
    const char* const Y0 = "r1bqkb1r/pppppppp/2n2n2/8/8/2N2N2/PPPPPPPP/R1BQKB1R w KQkq - 0 3";
    {
        Session g1;   // 1.Nf3 Nf6 2.Nc3 Nc6: Ng1 Ng8 meets nothing of this game
        for(const char* m : {"g1f3", "g8f6", "b1c3", "b8c6"})
        play(g1, m);
        check(g1.fen()==Y, "the moves reach Y", g1.fen());
        g1.set_analysis(iteration(16, 30, {"f3g1", "f6g8"}));
        check(persistent_tt().get(key(Y))!=nullptr, "a line that meets nothing of its game is kept");
        check(persistent_tt().get(key(Y0))==nullptr, "under its own halfmove clock only");
    }
    {
        Session fresh;   // Y at clock 4 with no history before it
        fresh.set_fen(Y, error);
        check(fresh.ptt_move_now(depth8, out) && out.pv[0]=="f3g1", "the same position and clock: played");
    }
    {
        Session zero;    // Y at clock 0: the 50-move rule stands elsewhere
        zero.set_fen(Y0, error);
        check(!zero.ptt_move_now(depth8, out), "another halfmove clock: not played");
        check(!zero.analysis_valid, "nor shown");
    }
    {
        Session g2;   // 1.Nc3 Nc6 2.Nf3 Nf6: Ng1 Ng8 puts 1.Nc3 Nc6 back
        for(const char* m : {"b1c3", "b8c6", "g1f3", "g8f6"})
        play(g2, m);
        check(g2.fen()==Y, "the other move order reaches Y too", g2.fen());
        check(!g2.ptt_move_now(depth8, out), "a line that runs into this game's history: not played");
        check(g2.ptt_hint().empty(), "nor seeded");
    }
    {
        Session rep;   // Y at clock 0, then Ng1 Ng8 Nf3 Nf6: Y again at clock 4
        rep.set_fen(Y0, error);
        for(const char* m : {"f3g1", "f6g8", "g1f3", "g8f6"})
        play(rep, m);
        check(key(rep.fen())==key(Y), "the knights' round trip reaches Y at clock 4", rep.fen());
        check(!rep.ptt_move_now(depth8, out), "a position repeated in this game: not played");
    }
    {
        Session off;   // the Engine panel's switch
        off.set_fen(Y, error);
        off.ptt_use = false;
        off.recall_kept();
        check(!off.ptt_move_now(depth8, out) && off.ptt_hint().empty(), "PTT off: nothing played or seeded");
        check(!off.analysis_valid, "nor shown");
        off.set_analysis(iteration(20, 25, {"e2e4"}));
        check(persistent_tt().get(key(Y))->info.depth==20, "but long searches are still stored");
        off.ptt_use = true;
        off.recall_kept();
        check(off.analysis_valid, "on again: shown at once");
    }
}

int main()
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    const std::string path = "/tmp/ascaniusfish_ptt_test_" + std::to_string(getpid()) + ".txt";
    test_round_trip(path);
    test_better(path);
    test_two_guis(path);
    test_session(path);
    test_history(path);
    test_use(path);
    std::remove(path.c_str());
    std::remove((path+".tmp").c_str());
    std::printf(failures ? "\n%d FAILED\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
