// OWNERSHIP=Claude
// make match A=<engine> B=<engine>: which of two engine builds is stronger?
//
//   ./tools/match <A> <B> [key=value ...]
//
// A and B are UCI engine binaries or git refs ("." = working tree), which
// are built as ascaniusfish_uci with a small TT (tt=) so many engines fit in
// memory. Every opening of the suite is played twice with colours swapped
// (a game pair), several games in parallel. The result is from B's point of
// view: W/D/L, score, and Elo B-A with a 95% CI from the pentanomial
// statistics of the game pairs. All games are written to a PGN file.
//
// With sprt=elo0,elo1 (the standard strength test: sprt=0,10, docs/WORKFLOW.md)
// the match is a sequential probability ratio test of H1 "B-A is elo1" against
// H0 "B-A is elo0" (logistic Elo, alpha = beta = 0.05), on the CCRL suite
// (tools/openings_ccrl.epd) unless openings= says otherwise. It prints the LLR
// with every pair and stops at a bound. Every match stops at its wall-time
// budget (time=, minutes): no new pairs start, running ones finish and count.
//
// Options:
//   depth=3            fixed depth for both engines (depthA=, depthB= per engine)
//   tc=<s>[+<inc>]     clock per game instead of a fixed depth, e.g. tc=10+0.1
//   concurrency=<n>    games in parallel (default: cores-1, capped by free memory)
//   pairs=<n>          only the first n openings
//   openings=<file>    EPD suite (default tools/openings.epd)
//   pgn=<file>         game output (default match.pgn)
//   sprt=<elo0>,<elo1> SPRT instead of a fixed number of pairs, e.g. sprt=0,10
//   time=<min>         wall-time budget, counted from after the builds (default 10;
//                      more only when Ascanius approves it)
//   tt=<n>             TT exponent for engines built from refs (default 17, 32MB)
//   optionsA=<n=v,...> UCI options for engine A (optionsB= for B), e.g.
//                      optionsA="UCI_LimitStrength=true,UCI_Elo=1500" for a
//                      Stockfish playing at a given CCRL rating
#include "git_build.hpp"
#include "game_rules.hpp"
#include "uci_engine.hpp"

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

static const char* const CXXFLAGS = "-O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -pthread";
constexpr int MAX_GAME_PLIES = 1000;          // adjudicated a draw beyond this; the rules end almost every game long before
constexpr long long DEPTH_MOVE_TIMEOUT_MS = 600000;
constexpr long long TC_GRACE_MS = 1000;       // how long past its clock an engine may take before it counts as hung

[[noreturn]] static void die(const std::string& msg)
{
    std::fprintf(stderr, "match: %s\n", msg.c_str());
    std::exit(2);
}

struct Limits
{
    int depth = 0;           // 0 = none
    long long base_ms = 0;   // 0 = no clock
    long long inc_ms = 0;
};

struct Opening
{
    std::string fen, name;
};

struct Player
{
    std::string label;
    Limits limits;
};

struct Game_Record
{
    int b_points2 = 0;   // B's points times 2 (0, 1, 2)
    std::string reason;
    std::string pgn;
};

// "{+0.35/3}" (mover's view, like UCI) from the last info line, or "";
// with a clock also the time the move took: "{+0.35/3 1.21s}".
static std::string score_comment(const std::string& info, long long used_ms)
{
    std::istringstream in(info);
    std::string tok, depth, kind, value;
    while(in >> tok)
    {
        if(tok=="depth") in >> depth;
        else if(tok=="score") in >> kind >> value;
    }
    char time[24] = "";
    if(used_ms>=0)
    std::snprintf(time, sizeof time, "%.2fs", used_ms/1000.0);
    if(kind.empty())
    return used_ms>=0 ? "{" + std::string(time) + "}" : "";
    char buf[64];
    if(kind=="mate")
    std::snprintf(buf, sizeof buf, "{%sM%s/%s%s%s}", value[0]=='-' ? "-" : "+", value.c_str()+(value[0]=='-'), depth.c_str(), *time ? " " : "", time);
    else
    std::snprintf(buf, sizeof buf, "{%+.2f/%s%s%s}", std::atoi(value.c_str())/100.0, depth.c_str(), *time ? " " : "", time);
    return buf;
}

static std::string today()
{
    std::time_t t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y.%m.%d", std::localtime(&t));
    return buf;
}

static std::string tc_string(const Limits& l)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%g+%g", l.base_ms/1000.0, l.inc_ms/1000.0);
    return buf;
}

// Plays one game; `b_is_white` says which engine has white.
static Game_Record play_game(Engine& white, Engine& black, const Player& wp, const Player& bp, bool b_is_white,
                             const Opening& opening, const std::string& round)
{
    Game_Record rec;
    std::string result, termination = "normal";
    Game* game = new Game;
    BB start;
    uci_parse_fen(opening.fen, start);
    std::istringstream fields(opening.fen);
    std::string skip;
    int halfmoves = 0, fullmove = 1;
    for(int i=0;i<4;i++) fields >> skip;
    fields >> halfmoves >> fullmove;
    game->start(start, halfmoves, fullmove);
    std::string opening_fen = game->fen();
    std::vector<std::string> comments;

    Engine* engines[2] = {&black, &white};  // indexed by white_to_move
    const Player* players[2] = {&bp, &wp};
    long long clock[2] = {bp.limits.base_ms, wp.limits.base_ms};
    for(Engine* e : engines)
    {
        e->send("ucinewgame");
        if(!e->ready() && !e->restart())
        die("engine " + e->path + " does not respond");
    }

    std::string position = "position fen " + opening_fen;
    for(;;)
    {
        Outcome o = game->outcome(rec.reason);
        if(o!=Outcome::ONGOING)
        {
            result = o==Outcome::WHITE_WINS ? "1-0" : o==Outcome::BLACK_WINS ? "0-1" : "1/2-1/2";
            break;
        }
        if((int)game->uci_moves.size()>=MAX_GAME_PLIES)
        {
            rec.reason = "adjudicated draw: game too long";
            result = "1/2-1/2";
            termination = "adjudication";
            break;
        }
        bool wtm = game->positions.back().white_move;
        Engine& e = *engines[wtm];
        const Limits& l = players[wtm]->limits;
        std::string go = "go";
        if(l.depth) go += " depth " + std::to_string(l.depth);
        if(l.base_ms)
        go += " wtime " + std::to_string(clock[1]) + " btime " + std::to_string(clock[0])
            + " winc " + std::to_string(wp.limits.inc_ms) + " binc " + std::to_string(bp.limits.inc_ms);

        std::string moves_part = game->uci_moves.empty() ? "" : " moves";
        for(const std::string& m : game->uci_moves) moves_part += " " + m;
        e.last_info.clear();
        long long t0 = now_ms();
        e.send(position + moves_part);
        e.send(go);
        std::string line;
        bool answered = e.wait_for("bestmove", l.base_ms ? clock[wtm]+TC_GRACE_MS : DEPTH_MOVE_TIMEOUT_MS, &line);
        long long used = now_ms()-t0;
        std::string loser = wtm ? "white" : "black";
        if(!answered)
        {
            rec.reason = loser + (l.base_ms ? " loses on time (no answer)" : " engine hung or crashed");
            termination = l.base_ms ? "time forfeit" : "abandoned";
            result = wtm ? "0-1" : "1-0";
            if(!e.restart()) die("engine " + e.path + " does not restart");
            break;
        }
        if(l.base_ms)
        {
            clock[wtm] -= used;
            if(clock[wtm]<0)
            {
                rec.reason = loser + " loses on time";
                termination = "time forfeit";
                result = wtm ? "0-1" : "1-0";
                break;
            }
            clock[wtm] += l.inc_ms;
        }
        std::istringstream in(line);
        std::string bestmove, move;
        in >> bestmove >> move;
        if(!game->play(move))
        {
            rec.reason = loser + " plays an illegal move " + move;
            termination = "rules infraction";
            result = wtm ? "0-1" : "1-0";
            break;
        }
        comments.push_back(score_comment(e.last_info, l.base_ms ? used : -1));
    }

    int white_points2 = result=="1-0" ? 2 : result=="0-1" ? 0 : 1;
    rec.b_points2 = b_is_white ? white_points2 : 2-white_points2;

    std::string pgn;
    auto tag = [&](const std::string& k, const std::string& v) { pgn += "[" + k + " \"" + v + "\"]\n"; };
    tag("Event", "match");
    tag("Site", "?");
    tag("Date", today());
    tag("Round", round);
    tag("White", wp.label);
    tag("Black", bp.label);
    tag("Result", result);
    tag("FEN", opening_fen);
    tag("SetUp", "1");
    tag("Opening", opening.name);
    tag("Termination", termination);
    tag("PlyCount", std::to_string(game->uci_moves.size()));
    if(wp.limits.base_ms) tag("TimeControl", tc_string(wp.limits));
    pgn += "\n";
    std::string text, row;
    bool white_first = game->positions[0].white_move;
    for(size_t i=0;i<game->san_moves.size();i++)
    {
        bool white = (i%2==0)==white_first;
        int number = game->start_fullmove + ((int)i+!white_first)/2;
        std::string token;
        if(white) token = std::to_string(number) + ". ";
        else if(i==0) token = std::to_string(number) + "... ";
        token += game->san_moves[i];
        if(!comments[i].empty()) token += " " + comments[i];
        if(row.size()+token.size()+1>80) { text += row + "\n"; row.clear(); }
        row += (row.empty() ? "" : " ") + token;
    }
    std::string ending = "{" + rec.reason + "} " + result;
    if(row.size()+ending.size()+1>80) { text += row + "\n"; row.clear(); }
    row += (row.empty() ? "" : " ") + ending;
    pgn += text + row + "\n\n";
    rec.pgn = pgn;
    delete game;
    return rec;
}

static std::vector<Opening> load_openings(const std::string& path)
{
    std::ifstream f(path);
    if(!f)
    die("cannot open " + path);
    std::vector<Opening> out;
    std::string line;
    while(std::getline(f, line))
    {
        if(line.empty() || line[0]=='#')
        continue;
        std::istringstream in(line);
        std::string field, fen;
        for(int i=0;i<4 && in>>field;i++) fen += (i ? " " : "") + field;
        std::string ops;
        std::getline(in, ops);
        auto opcode = [&](const std::string& name) -> std::string
        {
            size_t at = ops.find(" " + name + " ");
            if(at==std::string::npos) return "";
            at += name.size()+2;
            if(ops[at]=='"') return ops.substr(at+1, ops.find('"', at+1)-at-1);
            return ops.substr(at, ops.find(';', at)-at);
        };
        std::string hmvc = opcode("hmvc"), fmvn = opcode("fmvn");
        fen += " " + (hmvc.empty() ? "0" : hmvc) + " " + (fmvn.empty() ? "1" : fmvn);
        BB check;
        if(!uci_parse_fen(fen, check))
        die("bad position in " + path + ": " + line);
        out.push_back({fen, opcode("c0")});
    }
    return out;
}

// Pentanomial statistics: B's points per game pair (0, 0.5, ..., 2).
struct Stats
{
    int wins = 0, draws = 0, losses = 0;
    int penta[5] = {0, 0, 0, 0, 0};

    static double elo(double s)
    {
        s = std::min(std::max(s, 1e-6), 1-1e-6);
        return -400*std::log10(1/s-1);
    }

    int pairs() const { return penta[0]+penta[1]+penta[2]+penta[3]+penta[4]; }

    // Mean game score and the half-width of its 95% CI.
    void score(double& s, double& half) const
    {
        int n = pairs();
        s = 0;
        for(int k=0;k<5;k++) s += penta[k]*k/4.0;
        s /= std::max(n, 1);
        double var = 0;
        for(int k=0;k<5;k++) var += penta[k]*(k/4.0-s)*(k/4.0-s);
        var /= std::max(n, 1);
        half = 1.96*std::sqrt(var/std::max(n, 1));
    }

    // Log-likelihood ratio of H1 (B-A = elo1) against H0 (B-A = elo0), logistic
    // Elo (s = 1/(1+10^(-elo/400)) is the expected game score), the generalized
    // SPRT of fishtest: for each hypothesis the pentanomial distribution nearest
    // the observed one (maximum likelihood) whose mean pair score is s_i, then
    // LLR = sum over the cells of n_k log(p1_k/p0_k). That nearest distribution is
    // q_k = p_k / (1 + lambda (a_k - s)), a_k = k/4 the pair score, lambda the root
    // of sum q_k (a_k - s) = 0. An empty cell counts 1e-3 pairs. The normal
    // approximation n (s1-s0)(2m-s0-s1)/(2v) of cutechess and fastchess agrees on
    // ordinary data, but with one or a few pairs v is next to nothing and it
    // accepted H1 after a single 2-0 pair; this needs ~100 of them.
    double llr(double elo0, double elo1) const
    {
        int n = pairs();
        if(n==0)
        return 0;
        double p[5], total = 0;
        for(int k=0;k<5;k++)
        {
            p[k] = penta[k] ? penta[k] : 1e-3;
            total += p[k];
        }
        for(int k=0;k<5;k++) p[k] /= total;
        // log(q_k/p_k) of the distribution nearest p with mean s
        auto nearest = [&](double s, double log_ratio[5])
        {
            double lo = -1/(1-s), hi = 1/s;  // where 1 + lambda (a - s) > 0 for every a in [0, 1]
            for(int it=0;it<200;it++)
            {
                double mid = (lo+hi)/2, f = 0;  // f falls from +inf at lo to -inf at hi
                for(int k=0;k<5;k++) f += p[k]*(k/4.0-s)/(1+mid*(k/4.0-s));
                (f>0 ? lo : hi) = mid;
            }
            double lambda = (lo+hi)/2;
            for(int k=0;k<5;k++) log_ratio[k] = -std::log1p(lambda*(k/4.0-s));
        };
        auto expected = [](double elo) { return 1/(1+std::pow(10.0, -elo/400)); };
        double l0[5], l1[5];
        nearest(expected(elo0), l0);
        nearest(expected(elo1), l1);
        double sum = 0;
        for(int k=0;k<5;k++) sum += p[k]*(l1[k]-l0[k]);
        return n*sum;
    }

    std::string elo_string() const
    {
        double s, half;
        score(s, half);
        double lo = elo(s-half), hi = elo(s+half);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%+.1f ± %.1f", elo(s), (hi-lo)/2);
        return buf;
    }
};

static Player make_player(const std::string& label, int depth, const Limits& clock)
{
    Player p;
    p.label = label;
    p.limits = clock;
    p.limits.depth = depth;
    return p;
}

static long mem_available_kb()
{
    std::ifstream f("/proc/meminfo");
    std::string key;
    long value = 0;
    while(f >> key)
    if(key=="MemAvailable:" && f >> value) return value;
    return 0;
}

struct Side : Checkout
{
    std::string binary, build_log;
    bool built = false;
    std::vector<std::pair<std::string, std::string>> options;
};

// "Name=Value,Name=Value" -> setoption pairs. Names may hold spaces ("Skill Level=0").
static std::vector<std::pair<std::string, std::string>> parse_options(const std::string& text)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(text);
    std::string item;
    while(std::getline(in, item, ','))
    {
        size_t eq = item.find('=');
        if(eq==std::string::npos || eq==0 || eq+1==item.size())
        die("expected Name=Value in options, got " + item);
        out.push_back({item.substr(0, eq), item.substr(eq+1)});
    }
    return out;
}

// An existing executable is used as is; anything else is a git ref, checked
// out here and compiled by build().
static void prepare(Side& s, const std::string& root, const std::string& tag)
{
    struct stat st;
    if(stat(s.ref.c_str(), &st)==0 && S_ISREG(st.st_mode) && access(s.ref.c_str(), X_OK)==0)
    {
        s.binary = s.ref;
        s.label = s.ref.substr(s.ref.rfind('/')+1);
        return;
    }
    std::string error = checkout(s, root, "/tmp/ascaniusfish_match_" + tag);
    if(!error.empty())
    die(error);
    s.binary = "/tmp/ascaniusfish_match_uci_" + tag;
    s.built = true;
}

static void build(Side& s, int tt)
{
    if(!s.built)
    return;
    std::remove(s.binary.c_str());
    s.build_log = run_capture("cd '" + s.dir + "' && g++ " + CXXFLAGS + " -DTT_EXPONENT=" + std::to_string(tt)
                            + " -o '" + s.binary + "' ascaniusfish_uci.cpp 2>&1 | grep -E 'error' | head -5");
}

int main(int argc, char** argv)
{
    if(argc<3)
    {
        std::fprintf(stderr, "usage: %s <A> <B> [depth=3] [depthA=n] [depthB=n] [tc=s+inc] [concurrency=n] [pairs=n]\n"
                             "       [openings=tools/openings.epd] [pgn=match.pgn] [tt=17] [optionsA=Name=Value,...] [optionsB=...]\n"
                             "       [sprt=elo0,elo1] [time=10 (minutes)]\n"
                             "  A, B: UCI engine binary, or git ref to build (\".\" = working tree)\n", argv[0]);
        return 2;
    }
    std::map<std::string, std::string> opt;
    for(int i=3;i<argc;i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq==std::string::npos || eq==0)
        die("expected key=value, got " + a);
        if(eq+1<a.size())  // "key=" (as make passes unset variables) means the default
        opt[a.substr(0, eq)] = a.substr(eq+1);
    }
    auto get = [&](const std::string& k, const std::string& def) { return opt.count(k) ? opt[k] : def; };
    for(auto& [k, v] : opt)
    if(std::string(" depth depthA depthB tc concurrency pairs openings pgn tt optionsA optionsB sprt time ").find(" " + k + " ")==std::string::npos)
    die("unknown option " + k);

    Limits clock;
    if(!get("tc", "").empty())
    {
        std::string tc = get("tc", "");
        size_t plus = tc.find('+');
        clock.base_ms = (long long)(std::atof(tc.substr(0, plus).c_str())*1000);
        clock.inc_ms = plus==std::string::npos ? 0 : (long long)(std::atof(tc.substr(plus+1).c_str())*1000);
        if(clock.base_ms<=0) die("bad tc: " + tc);
    }
    int depth = std::atoi(get("depth", clock.base_ms ? "0" : "3").c_str());
    int depth_a = std::atoi(get("depthA", std::to_string(depth)).c_str());
    int depth_b = std::atoi(get("depthB", std::to_string(depth)).c_str());
    if((!depth_a || !depth_b) && !clock.base_ms)
    die("each engine needs a depth or a tc");

    bool sprt = opt.count("sprt");
    double elo0 = 0, elo1 = 0;
    if(sprt)
    {
        std::string bounds = opt["sprt"];
        size_t comma = bounds.find(',');
        if(comma==std::string::npos) die("expected sprt=elo0,elo1, got sprt=" + bounds);
        elo0 = std::atof(bounds.substr(0, comma).c_str());
        elo1 = std::atof(bounds.substr(comma+1).c_str());
        if(!(elo1>elo0)) die("sprt needs elo0 < elo1, got sprt=" + bounds);
    }
    const double ALPHA = 0.05, BETA = 0.05;
    const double llr_lower = std::log(BETA/(1-ALPHA)), llr_upper = std::log((1-BETA)/ALPHA);  // -+2.94
    double minutes = std::atof(get("time", "10").c_str());
    if(!(minutes>0)) die("time must be a positive number of minutes");

    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook
    signal(SIGPIPE, SIG_IGN);

    std::string root = repo_root();
    std::string suite = sprt ? "tools/openings_ccrl.epd" : "tools/openings.epd";
    if(sprt && !opt.count("openings") && access((root.empty() ? suite : root + "/" + suite).c_str(), R_OK)!=0)
    die(suite + " is missing; build it with tools/make_ccrl_openings (docs/BENCHMARKS.md)");
    std::vector<Opening> openings = load_openings(get("openings", root.empty() ? suite : root + "/" + suite));
    int pairs = std::min((int)openings.size(), std::atoi(get("pairs", std::to_string(openings.size())).c_str()));
    if(pairs<1) die("no openings");

    Side a, b;
    a.ref = argv[1];
    b.ref = argv[2];
    int tt = std::atoi(get("tt", "17").c_str());
    prepare(a, root, "A");  // worktrees one after the other (git locks), then both builds at once
    prepare(b, root, "B");
    a.options = parse_options(get("optionsA", ""));
    b.options = parse_options(get("optionsB", ""));
    for(Side* s : {&a, &b})
    for(const auto& [name, value] : s->options)
    s->label += " " + name + "=" + value;
    std::printf("building engines ...\n");
    std::fflush(stdout);
    std::thread build_b([&] { build(b, tt); });
    build(a, tt);
    build_b.join();
    for(Side* s : {&a, &b})
    if(access(s->binary.c_str(), X_OK)!=0)
    die("build failed for " + s->label + " (refs older than the UCI front end can't build):\n" + s->build_log);

    Player pa = make_player("A " + a.label + (depth_a && depth_a!=depth_b ? " d" + std::to_string(depth_a) : ""), depth_a, clock);
    Player pb = make_player("B " + b.label + (depth_b && depth_a!=depth_b ? " d" + std::to_string(depth_b) : ""), depth_b, clock);

    // The first engine pair tells how much memory a pair of games needs.
    int cores = std::max(1u, std::thread::hardware_concurrency());
    std::vector<Engine> ea(1), eb(1);
    ea[0].path = a.binary;
    eb[0].path = b.binary;
    ea[0].options = a.options;
    eb[0].options = b.options;
    if(!ea[0].start()) die("engine A does not start: " + a.binary);
    if(!eb[0].start()) die("engine B does not start: " + b.binary);
    long pair_kb = std::max(1L, ea[0].rss_kb()+eb[0].rss_kb());
    int by_memory = 1 + (int)(mem_available_kb()*0.8/pair_kb);
    int concurrency = opt.count("concurrency") ? std::atoi(opt["concurrency"].c_str()) : std::min({cores-1, by_memory, pairs});
    concurrency = std::max(1, std::min(concurrency, pairs));
    if(concurrency>by_memory)
    std::printf("warning: %d parallel games need ~%ld MB, more than is free\n", concurrency, concurrency*pair_kb/1024);

    std::string pgn_path = get("pgn", "match.pgn");
    std::ofstream pgn_file(pgn_path);
    if(!pgn_file) die("cannot write " + pgn_path);

    std::printf("A: %s\nB: %s\n", pa.label.c_str(), pb.label.c_str());
    if(sprt)
    std::printf("SPRT elo0 %g elo1 %g (logistic, alpha = beta = %g, LLR bounds %+.2f %+.2f), %g min budget\n",
                elo0, elo1, ALPHA, llr_lower, llr_upper, minutes);
    else
    std::printf("%g min budget\n", minutes);
    std::printf("%s%d openings x 2 colours = %d games, %s, %d in parallel (engines %ld MB per pair)\n\n",
                sprt ? "up to " : "", pairs, 2*pairs, clock.base_ms ? ("tc " + tc_string(clock)).c_str() : ("depth " + std::to_string(depth_a) + (depth_a!=depth_b ? "/" + std::to_string(depth_b) : "")).c_str(),
                concurrency, pair_kb/1024);
    std::fflush(stdout);

    std::mutex mutex;  // guards stats, reasons, pgn_file, stdout
    Stats stats;
    std::map<std::string, int> reasons;
    std::atomic<int> next_pair(0);
    int done = 0;
    long long start_ms = now_ms();
    long long deadline_ms = start_ms + (long long)(minutes*60000);
    std::atomic<bool> stop_new(false);  // a decision: no new pairs
    std::atomic<bool> time_up(false);
    int decision = 0, decided_at = 0;   // +1 H1, -1 H0, at that many pairs
    ea.resize(concurrency);
    eb.resize(concurrency);

    auto worker = [&](int w)
    {
        Engine& A = ea[w];
        Engine& B = eb[w];
        if(w>0)
        {
            A.path = a.binary;
            B.path = b.binary;
            A.options = a.options;
            B.options = b.options;
            if(!A.start() || !B.start()) die("engines do not start");
        }
        for(int p; ; )
        {
            if(stop_new)
            break;
            if(now_ms()>=deadline_ms)
            {
                time_up = true;
                break;
            }
            if((p = next_pair++) >= pairs)
            break;
            const Opening& o = openings[p];
            Game_Record g1 = play_game(A, B, pa, pb, false, o, std::to_string(p+1) + ".1");
            Game_Record g2 = play_game(B, A, pb, pa, true, o, std::to_string(p+1) + ".2");
            std::lock_guard<std::mutex> lock(mutex);
            for(const Game_Record* g : {&g1, &g2})
            {
                stats.wins += g->b_points2==2;
                stats.draws += g->b_points2==1;
                stats.losses += g->b_points2==0;
                std::string r = g->reason;
                if(r.compare(0, 6, "white ")==0 || r.compare(0, 6, "black ")==0) r = r.substr(6);
                reasons[r]++;
                pgn_file << g->pgn;
            }
            pgn_file.flush();
            stats.penta[g1.b_points2+g2.b_points2]++;
            done++;
            auto pts = [](int p2) { return p2==2 ? "1" : p2==1 ? "½" : "0"; };
            std::string llr_text;
            if(sprt)
            {
                double llr = stats.llr(elo0, elo1);
                char buf[32];
                std::snprintf(buf, sizeof buf, "  LLR %+.2f", llr);
                llr_text = buf;
                if(!decision && (llr>=llr_upper || llr<=llr_lower))
                {
                    decision = llr>=llr_upper ? 1 : -1;
                    decided_at = done;
                    stop_new = true;
                }
            }
            std::printf("[%3d/%d] B %s %s  %-48.48s  W%d D%d L%d  Elo %s%s\n", done, pairs, pts(g1.b_points2), pts(g2.b_points2),
                        o.name.c_str(), stats.wins, stats.draws, stats.losses, stats.elo_string().c_str(), llr_text.c_str());
            if(decided_at==done && decision)
            std::printf("%s accepted; the pairs still running finish and count\n", decision>0 ? "H1" : "H0");
            std::fflush(stdout);
        }
        A.stop();
        B.stop();
    };
    std::vector<std::thread> threads;
    for(int w=0;w<concurrency;w++)
    threads.emplace_back(worker, w);
    for(std::thread& t : threads)
    t.join();

    double s, half;
    stats.score(s, half);
    int games = stats.wins+stats.draws+stats.losses;
    double lo = Stats::elo(s-half), hi = Stats::elo(s+half);
    double los = half>0 ? 0.5*(1+std::erf((s-0.5)/(half/1.96)/std::sqrt(2.0))) : (s>0.5 ? 1 : s<0.5 ? 0 : 0.5);
    std::printf("\n%d games in %.0f s, PGN in %s\n", games, (now_ms()-start_ms)/1000.0, pgn_path.c_str());
    std::printf("endings:");
    for(auto& [r, n] : reasons) std::printf("  %s %d", r.c_str(), n);
    std::printf("\nB vs A: W %d  D %d  L %d   score %.1f%%\n", stats.wins, stats.draws, stats.losses, 100*s);
    std::printf("pairs (B points 0/0.5/1/1.5/2): %d %d %d %d %d\n", stats.penta[0], stats.penta[1], stats.penta[2], stats.penta[3], stats.penta[4]);
    std::printf("Elo B-A: %s  (95%% CI [%+.1f, %+.1f], LOS %.1f%%) -> %s\n", stats.elo_string().c_str(), lo, hi, 100*los,
                s-half>0.5 ? "B is stronger" : s+half<0.5 ? "B is weaker" : "no significant difference");
    int played = stats.pairs();
    if(sprt)
    {
        std::printf("SPRT [%g, %g]: LLR %+.2f (bounds %+.2f %+.2f), %d pairs\n", elo0, elo1, stats.llr(elo0, elo1),
                    llr_lower, llr_upper, played);
        if(decision>0)
        std::printf("H1 accepted after %d pairs: B is stronger than A (B-A nearer %g than %g Elo)\n", decided_at, elo1, elo0);
        else if(decision<0)
        std::printf("H0 accepted after %d pairs: B does not gain %g Elo over A (B-A nearer %g than %g)\n", decided_at, elo1, elo0, elo1);
        else
        {
            char why[64];
            if(time_up) std::snprintf(why, sizeof why, "the %g min budget is used up", minutes);
            else std::snprintf(why, sizeof why, "the openings are used up");
            std::printf("no decision, %s: LLR %+.2f, Elo %s, %d games. A budget this size decides only "
                        "differences well beyond that CI; a smaller real gain mostly ends here undecided "
                        "(the change is dropped, or Ascanius decides)\n",
                        why, stats.llr(elo0, elo1), stats.elo_string().c_str(), games);
        }
    }
    else if(time_up && played<pairs)
    std::printf("time budget of %g min used up after %d of %d pairs\n", minutes, played, pairs);

    for(Side* side : {&a, &b})
    {
        remove_checkout(*side, root);
        if(side->built) std::remove(side->binary.c_str());
    }
    return 0;
}
