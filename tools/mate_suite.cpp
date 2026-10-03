// OWNERSHIP=Claude
// Forced-mate suite (issue #78): does the engine find a mate in N within the
// time, and report exactly the shortest one, `mate N`?
//
//   zstd -dc ~/lichess/lichess_db_puzzle.csv.zst | ./tools/mate_suite make <out.epd>
//        [per_n=100] [long_n=10] [judge=~/stockfish15/src/stockfish] [judge_nodes=4000000] [judge_threads=4]
//   ./tools/mate_suite run [suite=tools/mate_suite.epd] [engine=./ascaniusfish_uci] [movetime=1000]
//        [concurrency=3] [judge=~/stockfish15/src/stockfish|none] [judge_nodes=2000000] [n=N] [log=]
//   ./tools/mate_suite claims [positions=bench|<file.epd>] [engine=] [movetime=1000] [concurrency=3]
//
// make: reads the Lichess puzzle database (CSV on stdin) and samples, with a
// fixed seed, `per_n` puzzles for each N = 2..5 and `long_n` for each N = 6..8
// (N = the solver's moves in the solution, which must match the puzzle's
// mateInN theme; mateIn5 is "5 or more"). The position is the one after the
// opponent's first move. A puzzle is kept only if the judge (Stockfish) also
// reports `mate N` from it. Line: <fen4> hmvc H; fmvn F; dm N; pv <uci...>; id "<PuzzleId>"
//
// run: one `go movetime` per position. A hit is a final `score mate N` with N
// = dm. Shorter than dm is a failure of the suite or of the engine's mate
// (exit 1); so is a negative mate. When the engine's move is not the puzzle's
// first move, the judge checks that it still mates in N (the opponent is
// mated in N-1 after it).
//
// claims: one `go movetime` per position of any list (`bench`: the bench's
// positions), counting the engine's checks of the mates its search claims
// (`info string mate claim ...`): confirmed, refuted (exit 1), unchecked.
#include "../lib/uci.hpp"
#include "bench_positions.hpp"
#include "game_rules.hpp"
#include "uci_engine.hpp"

#include <atomic>
#include <csignal>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <thread>

constexpr int MIN_N = 2, MAX_N = 8, LONG_N = 6;  // N >= LONG_N: the "few mates in 6-8"

struct Mate_Entry
{
    std::string fen;   // 6 fields
    int n = 0;
    std::vector<std::string> pv;
    std::string id;
};

static std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for(char c : s)
    {
        if(c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

static std::string arg(const std::map<std::string, std::string>& opt, const std::string& k, const std::string& def)
{
    auto it = opt.find(k);
    return it == opt.end() ? def : it->second;
}

// The judge's final score from `position` after `go nodes`: "mate N" -> N (the
// mover's view), 0 for no mate. `mated` is set when the mover has no move.
static int judge_mate(Engine& judge, const std::string& position, long long nodes, bool* no_move = nullptr)
{
    judge.send(position);
    judge.last_info.clear();
    judge.send("go nodes " + std::to_string(nodes));
    std::string line;
    if(!judge.wait_for("bestmove", 600000, &line)) return 0;
    if(no_move) *no_move = line.find("(none)") != std::string::npos;
    size_t at = judge.last_info.find(" score mate ");
    return at == std::string::npos ? 0 : std::atoi(judge.last_info.c_str() + at + 12);
}

static int make_suite(const std::string& out_path, const std::map<std::string, std::string>& opt)
{
    const int per_n = std::atoi(arg(opt, "per_n", "100").c_str());
    const int long_n = std::atoi(arg(opt, "long_n", "10").c_str());
    const long long judge_nodes = std::atoll(arg(opt, "judge_nodes", "4000000").c_str());
    Engine judge;
    judge.path = arg(opt, "judge", std::string(getenv("HOME")) + "/stockfish15/src/stockfish");
    judge.options = {{"Threads", arg(opt, "judge_threads", "4")}, {"Hash", "128"}};
    if(!judge.start()) { std::fprintf(stderr, "judge does not start: %s\n", judge.path.c_str()); return 2; }

    // Reservoir sampling per N, a few times the number wanted, so the judge can reject some.
    std::mt19937_64 rng(78);
    std::map<int, std::vector<std::vector<std::string>>> pool;
    std::map<int, long long> seen;
    std::string row;
    long long rows = 0;
    while(std::getline(std::cin, row))
    {
        if(rows++ == 0) continue;  // header
        if(row.find("mateIn") == std::string::npos) continue;
        std::vector<std::string> f = split(row, ',');
        if(f.size() < 8) continue;
        const int moves = (int)split(f[2], ' ').size();
        const int n = moves/2;
        if(moves % 2 || n < MIN_N || n > MAX_N) continue;
        if(f[7].find("mateIn" + std::to_string(std::min(n, 5))) == std::string::npos) continue;
        if(std::atoi(f[6].c_str()) < 100) continue;  // NbPlays: an established puzzle
        const int cap = 3*(n < LONG_N ? per_n : long_n);
        long long k = seen[n]++;
        auto& p = pool[n];
        if((int)p.size() < cap) p.push_back(f);
        else
        {
            long long j = std::uniform_int_distribution<long long>(0, k)(rng);
            if(j < cap) p[j] = f;
        }
    }
    std::ofstream out(out_path);
    out << "# OWNERSHIP=Claude\n# Forced-mate suite for tools/mate_suite, made by `tools/mate_suite make` (seed 78) from the\n"
           "# Lichess puzzle database; dm N = the side to move mates in N, confirmed by Stockfish 15.\n";
    for(int n = MIN_N; n <= MAX_N; n++)
    {
        const int want = n < LONG_N ? per_n : long_n;
        int kept = 0, shorter = 0, longer = 0, none = 0;
        for(const std::vector<std::string>& f : pool[n])
        {
            if(kept >= want) break;
            std::vector<std::string> moves = split(f[2], ' ');
            BB start;
            if(!uci_parse_fen(f[1], start)) continue;
            std::vector<std::string> fields = split(f[1], ' ');
            Game game;
            game.start(start, std::atoi(fields[4].c_str()), std::atoi(fields[5].c_str()));
            if(!game.play(moves[0])) continue;
            const std::string fen = game.fen();
            const int m = judge_mate(judge, "position fen " + fen, judge_nodes);
            if(m != n) { (m <= 0 ? none : m < n ? shorter : longer)++; continue; }
            std::vector<std::string> ff = split(fen, ' ');
            out << ff[0] << " " << ff[1] << " " << ff[2] << " " << ff[3] << " hmvc " << ff[4] << "; fmvn " << ff[5]
                << "; dm " << n << "; pv";
            for(size_t i = 1; i < moves.size(); i++) out << " " << moves[i];
            out << "; id \"" << f[0] << "\";\n";
            kept++;
        }
        std::printf("mate in %d: %lld puzzles, %d kept (judge rejected: %d shorter, %d longer, %d no mate)\n",
                    n, seen[n], kept, shorter, longer, none);
        std::fflush(stdout);
    }
    judge.stop();
    return 0;
}

static std::vector<Mate_Entry> load_suite(const std::string& path)
{
    std::ifstream f(path);
    if(!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    std::vector<Mate_Entry> out;
    std::string line;
    while(std::getline(f, line))
    {
        if(line.empty() || line[0] == '#') continue;
        std::vector<std::string> w = split(line, ' ');
        if(w.size() < 4) continue;
        auto field = [&](const std::string& name) -> std::string
        {
            size_t at = line.find(name + " ");
            if(at == std::string::npos) return "";
            size_t end = line.find(';', at);
            return line.substr(at + name.size() + 1, end - at - name.size() - 1);
        };
        Mate_Entry e;
        e.fen = w[0] + " " + w[1] + " " + w[2] + " " + w[3] + " " + field("hmvc") + " " + field("fmvn");
        e.n = std::atoi(field("dm").c_str());
        e.pv = split(field("pv"), ' ');
        std::string id = field("id");
        e.id = id.size() >= 2 ? id.substr(1, id.size() - 2) : id;
        BB check;
        if(!uci_parse_fen(e.fen, check) || e.n <= 0) { std::fprintf(stderr, "bad entry: %s\n", line.c_str()); std::exit(2); }
        out.push_back(e);
    }
    return out;
}

enum Verdict { HIT, LONGER, MISS, SHORTER, WRONG_SIGN, BAD_MOVE };
static const char* verdict_name[] = {"hit", "longer", "miss", "SHORTER", "WRONG SIGN", "BAD MOVE"};

struct Run_Result
{
    Verdict verdict = MISS;
    int mate = 0;              // the engine's final mate N, 0 for none
    long long first_ms = -1;   // time of the first info line with that final score
    std::string bestmove, note;
    std::vector<std::string> strings;  // the engine's "info string mate ..." lines
};

static Run_Result run_one(Engine& eng, Engine* judge, long long judge_nodes, const Mate_Entry& e, long long movetime)
{
    Run_Result r;
    eng.send("ucinewgame");
    if(!eng.ready() && !eng.restart()) { r.note = "engine does not respond"; return r; }
    eng.send("position fen " + e.fen);
    eng.send("go movetime " + std::to_string(movetime));
    long long deadline = now_ms() + movetime + 60000;
    std::string line, last_score;
    while(eng.read_line(line, deadline))
    {
        if(line.compare(0, 9, "bestmove ") == 0) { r.bestmove = split(line, ' ')[1]; break; }
        if(line.compare(0, 17, "info string mate ") == 0) { r.strings.push_back(line.substr(12)); continue; }
        Search_Info s;
        if(!parse_info(line, s) || s.score_kind.empty()) continue;
        std::string score = s.score_kind + " " + s.score_value;
        if(score != last_score) { last_score = score; r.first_ms = s.time_ms; }
        r.mate = s.score_kind == "mate" ? std::atoi(s.score_value.c_str()) : 0;
    }
    if(r.bestmove.empty()) { r.note = "no bestmove"; return r; }
    if(r.mate < 0) { r.verdict = WRONG_SIGN; return r; }
    if(r.mate == 0) { r.verdict = MISS; return r; }
    r.verdict = r.mate == e.n ? HIT : r.mate > e.n ? LONGER : SHORTER;
    if(r.bestmove != e.pv[0] && r.verdict != MISS)
    {
        if(!judge) r.note = "move differs, not judged";
        else
        {
            bool no_move = false;
            const int m = judge_mate(*judge, "position fen " + e.fen + " moves " + r.bestmove, judge_nodes, &no_move);
            const bool mates = r.mate == 1 ? no_move : m == -(r.mate - 1);
            r.note = mates ? "move differs, judge agrees" : "move differs, judge: mate " + std::to_string(m);
            if(!mates) r.verdict = BAD_MOVE;
        }
    }
    return r;
}

static int run_suite(const std::map<std::string, std::string>& opt)
{
    std::vector<Mate_Entry> suite = load_suite(arg(opt, "suite", "tools/mate_suite.epd"));
    const int only = std::atoi(arg(opt, "n", "0").c_str());
    if(only) suite.erase(std::remove_if(suite.begin(), suite.end(), [&](const Mate_Entry& e) { return e.n != only; }), suite.end());
    const std::string engine_path = arg(opt, "engine", "./ascaniusfish_uci");
    const std::string judge_path = arg(opt, "judge", std::string(getenv("HOME")) + "/stockfish15/src/stockfish");
    const long long judge_nodes = std::atoll(arg(opt, "judge_nodes", "2000000").c_str());
    const long long movetime = std::atoll(arg(opt, "movetime", "1000").c_str());
    const int concurrency = std::max(1, std::atoi(arg(opt, "concurrency", "3").c_str()));
    std::ofstream log(arg(opt, "log", "mate_suite.log"));

    std::vector<Run_Result> results(suite.size());
    std::atomic<int> next(0);
    std::mutex mutex;
    auto worker = [&]()
    {
        Engine eng, judge;
        eng.path = engine_path;
        judge.path = judge_path;
        judge.options = {{"Threads", "1"}, {"Hash", "64"}};
        if(!eng.start()) { std::fprintf(stderr, "engine does not start: %s\n", engine_path.c_str()); std::exit(2); }
        const bool judged = judge_path != "none" && judge.start();
        for(int i; (i = next++) < (int)suite.size(); )
        {
            results[i] = run_one(eng, judged ? &judge : nullptr, judge_nodes, suite[i], movetime);
            const Run_Result& r = results[i];
            std::lock_guard<std::mutex> lock(mutex);
            char buf[512];
            std::snprintf(buf, sizeof buf, "dm %d  %-6s %-10s mate %-3d at %5lld ms  bm %-5s  %s  %s", suite[i].n, suite[i].id.c_str(),
                          verdict_name[r.verdict], r.mate, r.first_ms, r.bestmove.c_str(), suite[i].fen.c_str(), r.note.c_str());
            log << buf << "\n";
            for(const std::string& s : r.strings) log << "    " << s << "\n";
            if(r.verdict != HIT) std::printf("%s\n", buf);
            std::fflush(stdout);
        }
        eng.stop();
        if(judged) judge.stop();
    };
    std::vector<std::thread> threads;
    for(int w = 0; w < concurrency; w++) threads.emplace_back(worker);
    for(std::thread& t : threads) t.join();

    std::printf("\n%-8s %5s %6s %7s %7s %6s %8s %8s %12s\n", "mate in", "pos", "hits", "hit %", "longer", "miss", "SHORTER", "wrong", "median ms");
    bool sound = true;
    for(int n = 1; n <= 20; n++)
    {
        int count = 0, c[6] = {0};
        std::vector<long long> times;
        for(size_t i = 0; i < suite.size(); i++)
        {
            if(suite[i].n != n) continue;
            count++;
            c[results[i].verdict]++;
            if(results[i].verdict == HIT) times.push_back(results[i].first_ms);
        }
        if(!count) continue;
        std::sort(times.begin(), times.end());
        sound = sound && !c[SHORTER] && !c[WRONG_SIGN] && !c[BAD_MOVE];
        std::printf("%-8d %5d %6d %6.1f%% %7d %6d %8d %8d %12lld\n", n, count, c[HIT], 100.0*c[HIT]/count, c[LONGER], c[MISS], c[SHORTER],
                    c[WRONG_SIGN] + c[BAD_MOVE], times.empty() ? -1LL : times[times.size()/2]);
    }
    return sound ? 0 : 1;
}

static int count_claims(const std::map<std::string, std::string>& opt)
{
    std::vector<std::string> fens;
    const std::string list = arg(opt, "positions", "bench");
    if(list == "bench")
    for(const char* f : BENCH_FENS) fens.push_back(f);
    else
    {
        std::ifstream f(list);
        if(!f) { std::fprintf(stderr, "cannot open %s\n", list.c_str()); return 2; }
        std::string line;
        while(std::getline(f, line))
        {
            if(line.empty() || line[0] == '#') continue;
            std::vector<std::string> w = split(line, ' ');
            if(w.size() >= 4) fens.push_back(w[0] + " " + w[1] + " " + w[2] + " " + w[3] + " 0 1");
        }
    }
    const std::string engine_path = arg(opt, "engine", "./ascaniusfish_uci");
    const long long movetime = std::atoll(arg(opt, "movetime", "1000").c_str());
    const int concurrency = std::max(1, std::atoi(arg(opt, "concurrency", "3").c_str()));
    std::atomic<int> next(0);
    std::mutex mutex;
    int claims = 0, confirmed = 0, refuted = 0, unchecked = 0;
    auto worker = [&]()
    {
        Engine eng;
        eng.path = engine_path;
        if(!eng.start()) { std::fprintf(stderr, "engine does not start: %s\n", engine_path.c_str()); std::exit(2); }
        for(int i; (i = next++) < (int)fens.size(); )
        {
            eng.send("ucinewgame");
            eng.ready();
            eng.send("position fen " + fens[i]);
            eng.send("go movetime " + std::to_string(movetime));
            std::string line;
            const long long deadline = now_ms() + movetime + 60000;
            std::vector<std::string> seen;
            while(eng.read_line(line, deadline) && line.compare(0, 9, "bestmove ") != 0)
            if(line.compare(0, 23, "info string mate claim ") == 0) seen.push_back(line.substr(12));
            std::lock_guard<std::mutex> lock(mutex);
            for(const std::string& c : seen)
            {
                claims++;
                if(c.find("confirmed") != std::string::npos) confirmed++;
                else if(c.find("refuted") != std::string::npos) { refuted++; std::printf("REFUTED  %s  %s\n", fens[i].c_str(), c.c_str()); }
                else unchecked++;
            }
        }
        eng.stop();
    };
    std::vector<std::thread> threads;
    for(int w = 0; w < concurrency; w++) threads.emplace_back(worker);
    for(std::thread& t : threads) t.join();
    std::printf("%zu positions: %d mate claims, %d confirmed, %d refuted, %d unchecked\n", fens.size(), claims, confirmed, refuted, unchecked);
    return refuted ? 1 : 0;
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        std::fprintf(stderr, "usage: %s make <out.epd> [per_n=] [long_n=] [judge=] [judge_nodes=]  (puzzle CSV on stdin)\n"
                             "       %s run [suite=] [engine=] [movetime=1000] [concurrency=3] [judge=|none] [n=N] [log=]\n"
                             "       %s claims [positions=bench|<file.epd>] [engine=] [movetime=1000] [concurrency=3]\n", argv[0], argv[0], argv[0]);
        return 2;
    }
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    signal(SIGPIPE, SIG_IGN);
    const std::string mode = argv[1];
    std::map<std::string, std::string> opt;
    for(int i = mode == "make" ? 3 : 2; i < argc; i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq == std::string::npos || eq == 0) { std::fprintf(stderr, "expected key=value, got %s\n", a.c_str()); return 2; }
        opt[a.substr(0, eq)] = a.substr(eq + 1);
    }
    if(mode == "make") return argc > 2 ? make_suite(argv[2], opt) : 2;
    if(mode == "run") return run_suite(opt);
    if(mode == "claims") return count_claims(opt);
    return 2;
}
