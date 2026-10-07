// OWNERSHIP=Claude
// Does stepping back show what the children already found (#100)? Ascanius's
// walk on the 3.Bc2+ study, in Analyse mode, the way the GUI does it: a
// gui/session.hpp Session holds the move tree and the Analysis_Store, one
// ./ascaniusfish_uci gets "position ... moves ...", the Session's
// "forget at ..." and "go infinite", and every iteration goes through
// Session::set_analysis(). No PTT (none is opened), no tablebases.
//
//   tools/walkback [engine=./ascaniusfish_uci] [walks=off,on,forget] [step_ms=1000] [key_ms=20000] [back_ms=1000] [back=11]
//
// Two key positions are analysed until they show their mate (at most key_ms):
// the M9 position at the end of the main line (5.Ke6 Nxd8+ 6.Kf5), then 5...Kg4
// 6.Qh4+, the refutation of the one defence at ply 9 that takes a while.
// Then `back` steps back from there towards the start (11 = all the way),
// back_ms each, as a person clicking back would; each step prints when a mate
// first showed in it. Walks, each on a fresh engine process:
//   off     the engine is off everywhere but the two key positions
//   on      the engine is on everywhere, step_ms per position (no forget: before #100)
//   forget  the same, with forget
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

struct Step
{
    int ply = 0;
    long long mate_ms = -1;   // when a mate first showed; -1 = none
    bool stored = false;      // that mate was a kept result, shown before the engine answered
    std::string score;        // what showed at the end
    int depth = 0;
    int forgot_positions = 0, forgot_entries = 0;
};

static std::string seconds(long long ms)
{
    return std::to_string(ms/1000) + "." + std::to_string(ms%1000/100) + " s";
}

// One analysis of the position on the cursor, as gui_server.cpp's
// maybe_start_search() starts it, for `run_ms` (or until a mate shows, with
// until_mate).
static Step analyse(Engine& engine, Session& s, bool forget, long long run_ms, bool until_mate)
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
    Session::Forget_Plan plan;
    if(forget)
    plan = s.forget_plan();
    if(!plan.command.empty())
    engine.send(plan.command);
    engine.send("go infinite");
    if(forget)
    s.forgot(plan);

    auto check = [&]() {
        if(step.mate_ms<0 && s.analysis_valid && s.analysis.score_kind=="mate")
        {
            step.mate_ms = now_ms()-t0;
            step.stored = s.analysis_stored;
        }
        return until_mate && step.mate_ms>=0;
    };
    bool done = check();
    const long long until = t0 + run_ms;
    std::string line;
    while(!done && now_ms()<until && engine.read_line(line, until))
    {
        Search_Info info;
        int n = 0, m = 0;
        if(std::sscanf(line.c_str(), "info string forget %d positions, %d entries demoted", &n, &m)==2)
        {
            step.forgot_positions = n;
            step.forgot_entries = m;
        }
        else if(parse_info(line, info))
        {
            s.set_analysis(info);
            done = check();
        }
    }
    engine.send("stop");
    while(engine.read_line(line, now_ms()+10000) && line.compare(0, 9, "bestmove ")!=0)
    {
        Search_Info info;
        if(parse_info(line, info) && !done)   // the stop's last iteration still counts
        s.set_analysis(info);
    }
    if(s.analysis_valid)
    {
        step.score = score_text(s.analysis);
        step.depth = s.analysis.depth;
    }
    return step;
}

static void print_step(const char* what, const Step& st, bool forget)
{
    std::printf("  %-4s ply %2d  %-11s %-8s depth %2d%s", what, st.ply,
                st.mate_ms<0 ? "no mate" : ("mate " + seconds(st.mate_ms)).c_str(),
                st.score.c_str(), st.depth, st.stored ? " (kept)" : "");
    if(forget)
    std::printf("  forgot %d positions, %d entries", st.forgot_positions, st.forgot_entries);
    std::printf("\n");
}

static bool walk(const std::string& engine_path, const std::string& name, long long step_ms, long long key_ms,
                 long long back_ms, int back)
{
    const bool engine_on = name!="off";
    const bool forget = name!="on";
    Engine engine;
    engine.path = engine_path;
    if(!engine.start())
    {
        std::cerr << "cannot start " << engine_path << "\n";
        return false;
    }
    Session s;
    std::string error;
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
    if(!engine.ready())
    return false;

    auto play = [&](const char* uci) {
        if(!s.play(uci, error))
        std::cerr << error << "\n";
        return error.empty();
    };
    std::printf("\n%s: the engine %s\n", name.c_str(),
                engine_on ? ("on everywhere, " + seconds(step_ms) + " per position").c_str() : "off but at the key positions");
    for(int i=0;i<STUDY_PLIES;i++)
    {
        if(engine_on)
        print_step("fwd", analyse(engine, s, forget, step_ms, false), forget);
        if(!play(STUDY_LINE[i]))
        return false;
    }
    print_step("key", analyse(engine, s, forget, key_ms, true), forget);
    while((int)s.moves().size()>SIDE_FROM)
    {
        s.navigate(Nav::BACK, error);
        if(engine_on)
        print_step("side", analyse(engine, s, forget, step_ms, false), forget);
    }
    for(int i=0;i<SIDE_PLIES;i++)
    {
        if(!play(SIDE_LINE[i]))
        return false;
        if(engine_on && i+1<SIDE_PLIES)
        print_step("side", analyse(engine, s, forget, step_ms, false), forget);
    }
    print_step("key", analyse(engine, s, forget, key_ms, true), forget);

    std::printf("%s: back, %s per step\n", name.c_str(), seconds(back_ms).c_str());
    int mates = 0;
    for(int k=0;k<back && !s.moves().empty();k++)
    {
        s.navigate(Nav::BACK, error);
        Step st = analyse(engine, s, forget, back_ms, false);
        mates += st.mate_ms>=0;
        print_step("back", st, forget);
    }
    std::printf("%s: %d steps back showed a mate\n", name.c_str(), mates);
    engine.send("quit");
    engine.stop(1000);
    return true;
}

int main(int argc, char** argv)
{
    std::string engine_path = "./ascaniusfish_uci";
    std::string walks = "off,on,forget";
    long long step_ms = 1000, key_ms = 20000, back_ms = 1000;
    int back = SIDE_FROM+SIDE_PLIES;
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
            std::cerr << "usage: tools/walkback [engine=./ascaniusfish_uci] [walks=off,on,forget] [step_ms=1000] [key_ms=20000] [back_ms=1000] [back=11]\n";
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
        if(name!="off" && name!="on" && name!="forget")
        {
            std::cerr << "unknown walk " << name << "\n";
            return 2;
        }
        if(!walk(engine_path, name, step_ms, key_ms, back_ms, back))
        return 1;
    }
    return 0;
}
