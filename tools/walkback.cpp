// OWNERSHIP=Claude
// Does Analyse show what the engine already found elsewhere in the move tree
// (#100, docs/plans/tt_knowledge_reuse.md)? Walks on the 3.Bc2+ study, the way
// the GUI does it: a gui/session.hpp Session holds the move tree and the
// Analysis_Store, one ./ascaniusfish_uci gets "position ... moves ..." and
// "go infinite", and every iteration goes through Session::set_analysis(). No
// PTT (none is opened), no tablebases.
//
//   tools/walkback [engine=./ascaniusfish_uci] [walks=off,on,fwd,mixed] [step_ms=1000] [key_ms=10000] [back_ms=1000] [back=4]
//
// A key position is analysed until it shows its mate (at most key_ms); every
// other analysed position runs step_ms (forward) or back_ms (back), as a person
// clicking along would. Each step prints when the page first showed a mate,
// when the engine first sent one (`engine`) and, if the engine reported it, how
// many TT entries it refreshed before the search. Walks, each on a fresh engine
// process:
//   off    the engine is off but at the two key positions (the M9 at the end
//          of the main line, then 5...Kg4 6.Qh4+), then `back` steps back
//   on     the same with the engine on everywhere
//   fwd    the engine off to the M9, then forward along its mate, back without
//          analysing and into a side line that transposes back into it
//   mixed  jumps, a side line and revisits around the two key positions
#include "../gui/session.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>

static const char* STUDY_FEN = "8/3P3k/n2K3p/2p3n1/1b4N1/2p1p1P1/8/3B4 w - - 0 1";
// 1.Nf6+ Kg7 2.Nh5+ Kg6 3.Bc2+ Kxh5 4.d8=Q Nf7+ 5.Ke6 Nxd8+ 6.Kf5, the M9 position
static const char* STUDY_LINE[] = {"g4f6", "h7g7", "f6h5", "g7g6", "d1c2", "g6h5", "d7d8q", "g5f7", "d6e6", "f7d8", "e6f5"};
static const int STUDY_PLIES = sizeof STUDY_LINE/sizeof STUDY_LINE[0];
// From ply 9 (after 5.Ke6): 5...Kg4 6.Qh4+
static const char* SIDE_LINE[] = {"h5g4", "d8h4"};
static const int SIDE_FROM = 9, SIDE_PLIES = 2;
// From the M9: 6...e2 7.Be4 e1=N 8.Bd5 c2 9.Bc4 c1=N 10.Bb5 Nc7 11.Ba4
static const char* MATE_LINE[] = {"e3e2", "c2e4", "e2e1n", "e4d5", "c3c2", "d5c4", "c2c1n", "c4b5", "a6c7", "b5a4"};
static const int MATE_PLIES = sizeof MATE_LINE/sizeof MATE_LINE[0];
// From ply 13 (after 7.Be4): 7...c2 8.Bd5 e1=N 9.Bc4, ply 17's position again
static const char* SWAP_LINE[] = {"c3c2", "e4d5", "e2e1n", "d5c4"};
static const int SWAP_PLIES = sizeof SWAP_LINE/sizeof SWAP_LINE[0];

struct Step
{
    int ply = 0;
    long long mate_ms = -1;         // when the page first showed a mate; -1 = none
    long long engine_mate_ms = -1;  // when the engine first sent one, whatever the page showed
    bool stored = false;            // the page's mate was a kept result, shown before the engine answered
    std::string score;              // what showed at the end
    int depth = 0;
    bool late = false;              // a mate showed only in the stop's last iteration
    int refreshed = -1;             // "info string refresh N entries demoted"; -1 = not reported
};

static std::string seconds(long long ms)
{
    return std::to_string(ms/1000) + "." + std::to_string(ms%1000/100) + " s";
}

// One analysis of the position on the cursor, as gui_server.cpp's
// maybe_start_search() starts it, for `run_ms` (or until a mate shows, with
// until_mate).
static Step analyse(Engine& engine, Session& s, long long run_ms, bool until_mate)
{
    Step step;
    step.ply = (int)s.moves().size();
    const long long t0 = now_ms();
    std::string position = "position fen " + s.root_fen();
    std::vector<std::string> moves = s.moves();
    if(!moves.empty())
    position += " moves";
    for(const std::string& m : moves)
    position += " " + m;
    engine.send(position);
    engine.send("go infinite");

    auto check = [&]() {
        if(step.mate_ms<0 && s.analysis_valid && s.analysis.score_kind=="mate")
        {
            step.mate_ms = now_ms()-t0;
            step.stored = s.analysis_stored;
        }
        return until_mate && step.mate_ms>=0;
    };
    auto engine_mate = [&](const Search_Info& info) {
        if(step.engine_mate_ms<0 && info.score_kind=="mate")
        step.engine_mate_ms = now_ms()-t0;
    };
    bool done = check();
    const long long until = t0 + run_ms;
    std::string line;
    while(!done && now_ms()<until && engine.read_line(line, until))
    {
        Search_Info info;
        int n = 0;
        if(std::sscanf(line.c_str(), "info string refresh %d entries demoted", &n)==1)
        step.refreshed = n;
        else if(parse_info(line, info))
        {
            engine_mate(info);
            s.set_analysis(info);
            done = check();
        }
    }
    engine.send("stop");
    while(engine.read_line(line, now_ms()+10000) && line.compare(0, 9, "bestmove ")!=0)
    {
        Search_Info info;
        if(parse_info(line, info) && !done)   // the stop's last iteration still counts
        {
            engine_mate(info);
            s.set_analysis(info);
        }
    }
    if(s.analysis_valid)
    {
        step.score = score_text(s.analysis);
        step.depth = s.analysis.depth;
        step.late = step.mate_ms<0 && s.analysis.score_kind=="mate";
    }
    return step;
}

static void print_step(const std::string& what, const Step& st)
{
    std::printf("  %-6s ply %2d  %-12s %-8s depth %2d  engine %-7s", what.c_str(), st.ply,
                st.mate_ms<0 ? (st.late ? "mate at stop" : "no mate") : ("mate " + seconds(st.mate_ms)).c_str(),
                st.score.c_str(), st.depth, st.engine_mate_ms<0 ? "-" : seconds(st.engine_mate_ms).c_str());
    if(st.refreshed>=0)
    std::printf("  refresh %d", st.refreshed);
    if(st.stored)
    std::printf("  (kept)");
    std::printf("\n");
}

struct Walker
{
    Engine engine;
    Session s;
    std::string error;
    long long step_ms = 1000, key_ms = 10000, back_ms = 1000;
    int steps = 0, mates = 0;   // the summary: analysed steps (key positions aside), and how many showed a mate

    bool start(const std::string& engine_path)
    {
        engine.path = engine_path;
        if(!engine.start())
        {
            std::cerr << "cannot start " << engine_path << "\n";
            return false;
        }
        if(!s.set_fen(STUDY_FEN, error))
        {
            std::cerr << error << "\n";
            return false;
        }
        s.start_analyse();
        s.ptt_use = false;
        for(const auto& option : s.engine_options())
        engine.send("setoption name " + option.first + " value " + option.second);
        engine.send("ucinewgame");
        return engine.ready();
    }
    void quit()
    {
        engine.send("quit");
        engine.stop(1000);
    }
    bool play(const char* uci)
    {
        error.clear();
        if(!s.play(uci, error))
        std::cerr << error << "\n";
        return error.empty();
    }
    bool go(Nav where, int times = 1)
    {
        for(int i=0;i<times;i++)
        if(!s.navigate(where, error))
        {
            std::cerr << error << "\n";
            return false;
        }
        return true;
    }
    // A key position: analysed until it shows its mate.
    void key()
    {
        Step st = analyse(engine, s, key_ms, true);
        print_step("key", st);
        if(st.mate_ms<0 && !st.late)
        std::printf("  (no mate within key_ms: the walk has nothing to build on, rerun it)\n");
    }
    // An analysed step, counted in the summary.
    Step step(const std::string& what, long long run_ms)
    {
        Step st = analyse(engine, s, run_ms, false);
        steps++;
        mates += st.mate_ms>=0;
        print_step(what, st);
        return st;
    }
    void summary(const std::string& name, const char* what)
    {
        std::printf("%s: %d of %d %s showed a mate\n", name.c_str(), mates, steps, what);
    }
};

// off and on: to the M9, back into 5...Kg4 6.Qh4+, then `back` steps back.
static bool walk_back(const std::string& engine_path, const std::string& name, Walker& w, int back)
{
    const bool engine_on = name=="on";
    std::printf("\n%s: the engine %s\n", name.c_str(),
                engine_on ? ("on everywhere, " + seconds(w.step_ms) + " per position").c_str() : "off but at the key positions");
    if(!w.start(engine_path))
    return false;
    for(int i=0;i<STUDY_PLIES;i++)
    {
        if(engine_on)
        print_step("fwd", analyse(w.engine, w.s, w.step_ms, false));
        if(!w.play(STUDY_LINE[i]))
        return false;
    }
    w.key();
    while((int)w.s.moves().size()>SIDE_FROM)
    {
        if(!w.go(Nav::BACK))
        return false;
        if(engine_on)
        print_step("side", analyse(w.engine, w.s, w.step_ms, false));
    }
    for(int i=0;i<SIDE_PLIES;i++)
    {
        if(!w.play(SIDE_LINE[i]))
        return false;
        if(engine_on && i+1<SIDE_PLIES)
        print_step("side", analyse(w.engine, w.s, w.step_ms, false));
    }
    w.key();

    std::printf("%s: back, %s per step\n", name.c_str(), seconds(w.back_ms).c_str());
    for(int k=0;k<back && !w.s.moves().empty();k++)
    {
        if(!w.go(Nav::BACK))
        return false;
        w.step("back", w.back_ms);
    }
    w.summary(name, "steps back");
    return true;
}

// fwd: the engine off to the M9, then forward along its mate, step_ms per
// position; back to ply 13 without analysing, and the side line 7...c2 8.Bd5
// e1=N 9.Bc4 that transposes into ply 17.
static bool walk_fwd(const std::string& engine_path, Walker& w)
{
    std::printf("\nfwd: the engine off to the M9, then on along its mate and a transposition, %s per position\n",
                seconds(w.step_ms).c_str());
    if(!w.start(engine_path))
    return false;
    for(int i=0;i<STUDY_PLIES;i++)
    if(!w.play(STUDY_LINE[i]))
    return false;
    w.key();
    for(int i=0;i<MATE_PLIES;i++)
    {
        if(!w.play(MATE_LINE[i]))
        return false;
        w.step("mate", w.step_ms);
    }
    if(!w.go(Nav::BACK, 8))
    return false;
    for(int i=0;i<SWAP_PLIES;i++)
    {
        if(!w.play(SWAP_LINE[i]))
        return false;
        w.step("swap", w.step_ms);
    }
    w.summary("fwd", "steps");
    return true;
}

// mixed: the plan's §5 table, one step label per row.
static bool walk_mixed(const std::string& engine_path, Walker& w)
{
    std::printf("\nmixed: jumps, a side line and revisits, %s per position\n", seconds(w.step_ms).c_str());
    if(!w.start(engine_path))
    return false;
    for(int i=0;i<STUDY_PLIES;i++)   // 1: plies 0-10, then 6.Kf5
    {
        print_step("m1", analyse(w.engine, w.s, w.step_ms, false));
        if(!w.play(STUDY_LINE[i]))
        return false;
    }
    w.key();                                           // 2: the M9
    if(!w.go(Nav::BACK, 2)) return false;              // 3: ply 9, after 5.Ke6
    w.step("m3", w.step_ms);
    if(!w.play("h5g4")) return false;                  // 4: ply 10', after 5...Kg4
    w.step("m4", w.step_ms);
    if(!w.play("d8h4")) return false;                  // 5: 6.Qh4+
    w.key();
    if(!w.go(Nav::BACK, 3)) return false;              // 6: ply 8, after 4...Nf7+
    w.step("m6", w.back_ms);
    if(!w.go(Nav::FORWARD) || !w.play("f7g5"))         // 7: ply 10'', after 5...Ng5+
    return false;
    w.step("m7", w.step_ms);
    if(!w.go(Nav::BACK)) return false;                 // 8: ply 9
    w.step("m8", w.back_ms);
    if(!w.go(Nav::FORWARD)) return false;              // 9: ply 10, after 5...Nxd8+
    w.step("m9", w.step_ms);
    if(!w.go(Nav::START)) return false;                // 10: ply 0
    w.step("m10", w.back_ms);
    w.summary("mixed", "steps");
    return true;
}

int main(int argc, char** argv)
{
    std::string engine_path = "./ascaniusfish_uci";
    std::string walks = "off,on,fwd,mixed";
    long long step_ms = 1000, key_ms = 10000, back_ms = 1000;
    int back = 4;   // plies 10-7: before ply 7 nothing shows a mate in seconds (4...Kg4)
    for(int i=1;i<argc;i++)
    {
        std::string arg = argv[i];
        size_t eq = arg.find('=');
        std::string key = arg.substr(0, eq), value = eq==std::string::npos ? "" : arg.substr(eq+1);
        if(key=="engine") engine_path = value;
        else if(key=="walks") walks = value;
        else if(key=="step_ms") step_ms = std::atoll(value.c_str());
        else if(key=="key_ms") key_ms = std::atoll(value.c_str());
        else if(key=="back_ms") back_ms = std::atoll(value.c_str());
        else if(key=="back") back = std::max(1, std::atoi(value.c_str()));
        else
        {
            std::cerr << "usage: tools/walkback [engine=./ascaniusfish_uci] [walks=off,on,fwd,mixed] [step_ms=1000] [key_ms=10000] [back_ms=1000] [back=4]\n";
            return 2;
        }
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // a long run shows as it goes
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    size_t at = 0;
    while(at<=walks.size())
    {
        size_t comma = walks.find(',', at);
        std::string name = walks.substr(at, comma==std::string::npos ? std::string::npos : comma-at);
        at = comma==std::string::npos ? walks.size()+1 : comma+1;
        Walker w;
        w.step_ms = step_ms;
        w.key_ms = key_ms;
        w.back_ms = back_ms;
        bool ok;
        if(name=="off" || name=="on")
        ok = walk_back(engine_path, name, w, back);
        else if(name=="fwd")
        ok = walk_fwd(engine_path, w);
        else if(name=="mixed")
        ok = walk_mixed(engine_path, w);
        else
        {
            std::cerr << "unknown walk " << name << "\n";
            return 2;
        }
        w.quit();
        if(!ok)
        return 1;
    }
    return 0;
}
