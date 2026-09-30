// OWNERSHIP=Claude
// Trade suite (issue #39): 6-7 piece positions where the right trade decides the
// result, so a search that reads the tables at 5 pieces can tell the trades apart
// and one that cannot has to guess.
//
//   ./tools/tb_trade_suite make <tables dir> <out.epd> [count=60]
//   ./tools/tb_trade_suite run  <tables dir> [suite=tools/tb_trade_suite.epd] [engine=./ascaniusfish_uci]
//                               [depth=8 | movetime=ms] [tables=on|off] [probe_limit=5] [concurrency=n]
//
// A position is in the suite when the side to move has a capture that keeps a
// tablebase win and another that throws it away. "Trade" is a capture into a
// position the tables know, either at once (6 pieces -> 5) or when the reply
// is forced and is itself a capture (7 -> 6 -> 5). The tables of our own
// prober (lib/syzygy.hpp) decide which trades win; nothing here searches.
// Positions are picked so that the losing trade takes at least as much material
// as the winning one, otherwise a material count alone would find the right one.
//
// run: the engine gets each position, and it is solved when its move is one of
// the winning trades. A move that is not a trade counts as unsolved even if it
// might win too: the tables cannot say. tables=off is the baseline. The count
// goes to stdout, exit 0 always (it is a measurement, not a test).
//
// Line: <fen4> hmvc 0; fmvn 1; win e2e4 ...; lose d1d5 ...; c0 "KRPvKRP"
#include "../lib/uci.hpp"
#include "syzygy_positions.hpp"
#include "uci_engine.hpp"

#include <atomic>
#include <csignal>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

struct Trade_Entry
{
    std::string fen, name;
    std::vector<std::string> win, lose;
};

static int piece_total(const BB& pos)
{
    int n = 0;
    for(int i=0;i<12;i++)
    n += __builtin_popcountll(pos.Board[i]);
    return n;
}

static const int PIECE_VALUE[6] = {1, 5, 3, 3, 9, 0};

static int material_of(const BB& pos)
{
    int v = 0;
    for(int i=0;i<12;i++)
    v += PIECE_VALUE[i%6] * __builtin_popcountll(pos.Board[i]);
    return v;
}

// What the side to move of `pos` gets from playing into `child`: +2 win .. -2 loss
// from the mover's view, or INT_MIN when the tables cannot say (too many pieces
// and no forced recapture).
static int trade_value(const BB& child, int depth = 0)
{
    int wdl;
    if(piece_total(child) <= 5)
    return syzygy::probe_wdl(&child, wdl) ? -wdl : INT_MIN;
    if(depth > 0)
    return INT_MIN;
    BB replies[256];
    if(std::get<0>(all_moves(&child, replies)) != 1 || piece_total(replies[0]) >= piece_total(child))
    return INT_MIN;
    const int again = trade_value(replies[0], depth+1);   // the replier's view is reversed twice
    return again == INT_MIN ? INT_MIN : -again;
}

static bool analyse(const std::string& fen4, Trade_Entry& out)
{
    BB pos;
    if(!uci_parse_fen(fen4 + " 0 1", pos))
    return false;
    BB kids[256];
    auto result = all_moves(&pos, kids);
    const int n = std::get<0>(result);
    int best_win_take = -1, best_lose_take = -1;
    const int before = material_of(pos);
    Trade_Entry e;
    e.fen = fen4;
    for(int i=0;i<n;i++)
    {
        if(piece_total(kids[i]) >= piece_total(pos))
        continue;   // not a capture
        const int v = trade_value(kids[i]);
        if(v == INT_MIN)
        continue;
        // what the capture took, in pawns
        const int take = std::abs(before - material_of(kids[i]));
        const std::string uci = get_UCI(&pos, kids+i);
        if(v == 2) { e.win.push_back(uci); best_win_take = std::max(best_win_take, take); }
        else { e.lose.push_back(uci); best_lose_take = std::max(best_lose_take, take); }
    }
    if(e.win.empty() || e.lose.empty() || best_lose_take < best_win_take)
    return false;
    out = e;
    return true;
}

static int make_suite(const std::string& dir, const std::string& out_path, int count)
{
    if(syzygy::init(dir) == 0) { std::fprintf(stderr, "no tables in %s\n", dir.c_str()); return 2; }
    std::mt19937_64 rng(39);
    // Materials with a trade into a 5-piece table (6 pieces), and 7-piece ones.
    const char* materials[] = {"KRPvKRP", "KQPvKQP", "KRBvKRP", "KRNvKRP", "KBPvKBP", "KNPvKNP", "KQRvKQR",
                               "KRPvKBP", "KRPvKNP", "KQPvKRP", "KQBvKRP", "KBBvKRP", "KRRvKQP",
                               "KRPPvKRP", "KRPvKRPP", "KQPPvKQP", "KBPPvKBP", "KRBPvKRP", "KRPPvKBP", "KQRPvKQR"};
    std::vector<Trade_Entry> found;
    std::map<std::string, int> per_material;
    const int per_limit = std::max(3, count / 12);
    const size_t six_count = 13, all_count = sizeof materials / sizeof *materials;
    // 7-piece positions first (a forced recapture is rare), then the 6-piece ones.
    auto collect = [&](size_t first, size_t last, size_t target, long max_tries)
    {
        for(long tries = 0; found.size() < target && tries < max_tries; tries++)
        {
            const std::string material = materials[first + tries % (last - first)];
            if(per_material[material] >= per_limit)
            continue;
            int counts[12];
            tb_material(material, counts);
            const bool white = (tries / 7) % 2 == 0;
            const std::string fen = tb_random_fen(counts, false, white, false, rng);
            if(fen.empty())
            continue;
            Trade_Entry e;
            if(!analyse(fen.substr(0, fen.rfind(' ', fen.rfind(' ')-1)), e))
            continue;
            e.name = material;
            per_material[material]++;
            found.push_back(e);
        }
    };
    collect(six_count, all_count, count / 6, 20000000);
    collect(0, six_count, count, 20000000);
    if(found.size() < (size_t)count)
    collect(0, all_count, count, 20000000);
    std::ofstream f(out_path);
    f << "# OWNERSHIP=Claude\n# Trade suite for tools/tb_trade_suite, made by `tools/tb_trade_suite make` (seed 39).\n"
         "# win: captures into a tablebase win; lose: captures that give it away (and take at least as much).\n";
    int sevens = 0;
    for(const Trade_Entry& e : found)
    {
        f << e.fen << " hmvc 0; fmvn 1; win";
        for(const std::string& m : e.win) f << " " << m;
        f << "; lose";
        for(const std::string& m : e.lose) f << " " << m;
        f << "; c0 \"" << e.name << "\"\n";
        sevens += e.name.size() > 7;
    }
    std::printf("%zu positions (%d with 7 pieces) -> %s\n", found.size(), sevens, out_path.c_str());
    return found.size() == (size_t)count ? 0 : 1;
}

static std::vector<Trade_Entry> load_suite(const std::string& path)
{
    std::ifstream f(path);
    if(!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    std::vector<Trade_Entry> out;
    std::string line;
    while(std::getline(f, line))
    {
        if(line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        Trade_Entry e;
        std::string field;
        for(int i=0; i<4 && in >> field; i++) e.fen += (i ? " " : "") + field;
        std::string ops;
        std::getline(in, ops);
        auto moves_of = [&](const std::string& key, std::vector<std::string>& into)
        {
            size_t at = ops.find(" " + key);
            if(at == std::string::npos) return;
            std::istringstream words(ops.substr(at + key.size() + 1, ops.find(';', at) - at - key.size() - 1));
            for(std::string m; words >> m; ) into.push_back(m);
        };
        moves_of("win", e.win);
        moves_of("lose", e.lose);
        size_t c = ops.find("c0 \"");
        if(c != std::string::npos) e.name = ops.substr(c+4, ops.find('"', c+4)-c-4);
        out.push_back(e);
    }
    return out;
}

int main(int argc, char** argv)
{
    if(argc < 3)
    {
        std::fprintf(stderr, "usage: %s make <tables dir> <out.epd> [count=60]\n       %s run <tables dir> [suite=] [engine=] [depth=8|movetime=ms] [tables=on|off] [probe_limit=5] [concurrency=n]\n", argv[0], argv[0]);
        return 2;
    }
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    signal(SIGPIPE, SIG_IGN);
    const std::string mode = argv[1], dir = argv[2];
    std::map<std::string, std::string> opt;
    for(int i = mode == "make" ? 4 : 3; i < argc; i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq == std::string::npos || eq == 0) { std::fprintf(stderr, "expected key=value, got %s\n", a.c_str()); return 2; }
        if(eq + 1 < a.size()) opt[a.substr(0, eq)] = a.substr(eq + 1);
    }
    auto get = [&](const std::string& k, const std::string& def) { return opt.count(k) ? opt[k] : def; };
    if(mode == "make")
    return argc > 3 ? make_suite(dir, argv[3], std::atoi(get("count", "60").c_str())) : 2;
    if(mode != "run") return 2;

    const std::vector<Trade_Entry> suite = load_suite(get("suite", "tools/tb_trade_suite.epd"));
    const std::string engine_path = get("engine", "./ascaniusfish_uci");
    const long long movetime = std::atoll(get("movetime", "0").c_str());
    const int depth = std::atoi(get("depth", "8").c_str());
    const bool tables_on = get("tables", "on") != "off";
    const int cores = std::max(1u, std::thread::hardware_concurrency());
    const int concurrency = std::max(1, std::atoi(get("concurrency", std::to_string(std::min(2, std::max(1, cores/2)))).c_str()));
    const std::string go = movetime > 0 ? "go movetime " + std::to_string(movetime) : "go depth " + std::to_string(depth);

    std::vector<int> outcome(suite.size(), 0);   // 1 solved, 0 wrong trade, -1 not a trade
    std::atomic<int> next(0);
    std::mutex mutex;
    auto worker = [&]()
    {
        Engine engine;
        engine.path = engine_path;
        if(tables_on)
        {
            engine.options.push_back({"SyzygyPath", dir});
            engine.options.push_back({"SyzygyProbeLimit", get("probe_limit", "5")});
        }
        if(!engine.start()) { std::fprintf(stderr, "engine does not start: %s\n", engine_path.c_str()); std::exit(2); }
        for(int i; (i = next++) < (int)suite.size(); )
        {
            const Trade_Entry& e = suite[i];
            engine.send("ucinewgame");
            engine.send("position fen " + e.fen + " 0 1");
            engine.send(go);
            std::string line;
            std::string move = "?";
            if(engine.wait_for("bestmove", 120000, &line))
            move = line.substr(9, line.find(' ', 9) - 9);
            const bool solved = std::find(e.win.begin(), e.win.end(), move) != e.win.end();
            const bool wrong = std::find(e.lose.begin(), e.lose.end(), move) != e.lose.end();
            outcome[i] = solved ? 1 : wrong ? 0 : -1;
            std::lock_guard<std::mutex> lock(mutex);
            std::printf("%-9s %-52s %-6s %s\n", e.name.c_str(), e.fen.c_str(), move.c_str(), solved ? "solved" : wrong ? "WRONG TRADE" : "other move");
            std::fflush(stdout);
        }
        engine.stop();
    };
    std::vector<std::thread> threads;
    for(int w = 0; w < concurrency; w++) threads.emplace_back(worker);
    for(std::thread& t : threads) t.join();
    int solved = 0, wrong = 0;
    for(int o : outcome) { solved += o == 1; wrong += o == 0; }
    std::printf("\ntables %s, %s: solved %d/%d (%d wrong trade, %zu other move)\n", tables_on ? "on" : "off", go.c_str(), solved, (int)suite.size(), wrong, suite.size() - solved - wrong);
    return 0;
}
