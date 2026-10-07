// OWNERSHIP=Claude
// Does stepping back show what the child already found (#100)? Walks the 3.Bc2+
// study line forward to the M9 position and then back to the start, in Analyse
// mode, the way the GUI does: a gui/session.hpp Session holds the move tree and
// the Analysis_Store, one ./ascaniusfish_uci gets "position ... moves ...",
// the Session's "forget at ..." and "go infinite", and every iteration goes
// through Session::set_analysis(). No PTT (none is opened), no tablebases.
//
//   tools/walkback [engine=./ascaniusfish_uci] [walks=off,on,forget] [step_ms=10000] [cap_ms=60000] [back=11]
//
// Walks, each on a fresh engine process:
//   off     forward with the engine off, then on at the M9 position (with forget)
//   on      forward with the engine on, step_ms per position (no forget: before #100)
//   forget  the same, with forget
// Every walk analyses the M9 position for step_ms. Each step back then waits
// until the page shows a mate, or until cap_ms, and prints the time it took.
// The first walk sets the targets: where it showed no mate within cap_ms, a
// later walk waits for that walk's depth instead. `back` is how many steps
// back are walked (11 = to the start; 3 = the ones that mate: M10, M10, M11).
#include "../gui/session.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>

static const char* STUDY_FEN = "8/3P3k/n2K3p/2p3n1/1b4N1/2p1p1P1/8/3B4 w - - 0 1";
// 1.Nf6+ Kg7 2.Nh5+ Kg6 3.Bc2+ Kxh5 4.d8=Q Nf7+ 5.Ke6 Nxd8+ 6.Kf5, the M9 position
static const char* STUDY_LINE[] = {"g4f6", "h7g7", "f6h5", "g7g6", "d1c2", "g6h5", "d7d8q", "g5f7", "d6e6", "f7d8", "e6f5"};
static const int STUDY_PLIES = sizeof STUDY_LINE/sizeof STUDY_LINE[0];

struct Target
{
    bool mate = true;
    int depth = 0;   // when !mate
};

struct Step
{
    int ply = 0;
    long long ms = -1;        // time until the target showed; -1 = not within cap_ms
    std::string score;        // what showed then (or at cap_ms)
    int depth = 0;
    bool stored = false;      // it was a kept result, shown before the engine answered
    int forgot_positions = 0, forgot_entries = 0;
    bool mate = false;
};

static bool reached(const Session& s, const Target& t)
{
    if(!s.analysis_valid)
    return false;
    return t.mate ? s.analysis.score_kind=="mate" : s.analysis.depth>=t.depth;
}

// One analysis of the position on the cursor, as gui_server.cpp's
// maybe_start_search() starts it. Runs for `run_ms`, or, with a target, until
// the page shows it or cap_ms passes.
static Step analyse(Engine& engine, Session& s, bool forget, long long run_ms, const Target* target, long long cap_ms)
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

    auto done = [&]() {
        if(!target || !reached(s, *target))
        return false;
        step.ms = now_ms()-t0;
        step.stored = s.analysis_stored;
        return true;
    };
    bool hit = done();
    const long long until = t0 + (target ? cap_ms : run_ms);
    std::string line;
    while(!hit && now_ms()<until && engine.read_line(line, until))
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
            hit = done();
        }
    }
    engine.send("stop");
    while(engine.read_line(line, now_ms()+10000) && line.compare(0, 9, "bestmove ")!=0)
    {
        Search_Info info;
        if(parse_info(line, info) && !hit)   // the stop's last iteration still counts
        s.set_analysis(info);
    }
    if(s.analysis_valid)
    {
        step.score = score_text(s.analysis);
        step.depth = s.analysis.depth;
        step.mate = s.analysis.score_kind=="mate";
    }
    return step;
}

static bool walk(const std::string& engine_path, const std::string& name, long long step_ms, long long cap_ms, int back,
                 std::vector<Target>& targets, bool set_targets)
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

    std::printf("\n%s: forward %s, %lld s per position\n", name.c_str(), engine_on ? "with the engine on" : "with the engine off", step_ms/1000);
    for(int i=0;i<=STUDY_PLIES;i++)
    {
        if(engine_on || i==STUDY_PLIES)
        {
            Step st = analyse(engine, s, forget, step_ms, nullptr, 0);
            std::printf("  fwd ply %2d  %-8s depth %2d", st.ply, st.score.c_str(), st.depth);
            if(forget)
            std::printf("  forgot %d positions, %d entries", st.forgot_positions, st.forgot_entries);
            std::printf("\n");
        }
        if(i<STUDY_PLIES && !s.play(STUDY_LINE[i], error))
        {
            std::cerr << error << "\n";
            return false;
        }
    }
    std::printf("%s: back, each step until %s (cap %lld s)\n", name.c_str(), set_targets ? "a mate shows" : "the first walk's result shows", cap_ms/1000);
    long long total = 0;
    for(int k=0;k<back;k++)
    {
        s.navigate(Nav::BACK, error);
        Target t = set_targets ? Target{} : targets[k];
        Step st = analyse(engine, s, forget, 0, &t, cap_ms);
        if(set_targets)
        targets.push_back(st.mate ? Target{} : Target{false, st.depth});
        total += st.ms<0 ? cap_ms : st.ms;
        std::printf("  back ply %2d  %-11s %8s  %-8s depth %2d%s", st.ply,
                    t.mate ? "mate" : ("depth " + std::to_string(t.depth)).c_str(),
                    st.ms<0 ? "> cap" : (std::to_string(st.ms/1000) + "." + std::to_string(st.ms%1000/100) + " s").c_str(),
                    st.score.c_str(), st.depth, st.stored ? " (kept)" : "");
        if(forget)
        std::printf("  forgot %d positions, %d entries", st.forgot_positions, st.forgot_entries);
        std::printf("\n");
    }
    std::printf("%s: back total %.1f s\n", name.c_str(), total/1000.0);
    engine.send("quit");
    engine.stop(1000);
    return true;
}

int main(int argc, char** argv)
{
    std::string engine_path = "./ascaniusfish_uci";
    std::string walks = "off,on,forget";
    long long step_ms = 10000, cap_ms = 60000;
    int back = STUDY_PLIES;
    for(int i=1;i<argc;i++)
    {
        std::string arg = argv[i];
        size_t eq = arg.find('=');
        std::string key = arg.substr(0, eq), value = eq==std::string::npos ? "" : arg.substr(eq+1);
        if(key=="engine") engine_path = value;
        else if(key=="walks") walks = value;
        else if(key=="step_ms") step_ms = std::atoll(value.c_str());
        else if(key=="cap_ms") cap_ms = std::atoll(value.c_str());
        else if(key=="back") back = std::max(1, std::min(STUDY_PLIES, std::atoi(value.c_str())));
        else
        {
            std::cerr << "usage: tools/walkback [engine=./ascaniusfish_uci] [walks=off,on,forget] [step_ms=10000] [cap_ms=60000] [back=11]\n";
            return 2;
        }
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // a long run shows as it goes
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::vector<Target> targets;
    bool first = true;
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
        if(!walk(engine_path, name, step_ms, cap_ms, back, targets, first))
        return 1;
        first = false;
    }
    return 0;
}
