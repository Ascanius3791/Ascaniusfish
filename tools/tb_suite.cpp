// OWNERSHIP=Claude
// Tablebase endgame suite (issue #38): does the engine with tables win what is
// won within the 50-move rule, and never lose what is drawn?
//
//   ./tools/tb_suite make <tables dir> <out.epd>
//   ./tools/tb_suite run  <tables dir> [suite=tools/tb_suite.epd] [engine=./ascaniusfish_uci]
//                         [movetime=1000] [opp_movetime=100] [concurrency=n] [tables=on|off] [pgn=tb_suite.pgn]
//                         [gaviota=<dtm tables dir>] [opp_gaviota=on]
//
// make: writes the suite with a fixed seed, positions picked with our own
// prober (lib/syzygy.hpp). Won positions have the side to move winning with a
// finite DTZ; KBNvK is the hardest 6 of many by DTZ. Drawn positions are
// WDL 0. Line: <fen4> hmvc 0; fmvn 1; wdl 2|0; dtz N; c0 "KQvK"
//
// run: the engine under test (tables on, `movetime` per move) plays the
// winning side against the same binary without tables (`opp_movetime`, its
// strength hardly matters). A drawn position is played twice, once from each
// colour. A won game passes only if the engine mates; 50-move draws and
// repetitions fail. A drawn game passes if the engine does not lose.
// Exits 1 if any game fails. tables=off is the baseline without the tables.
//
// gaviota=<dir> gives the tested engine the Gaviota DTM tables too (#77): then
// a won game also needs a `mate N` from every move of the tested engine, with
// N at least one smaller each move; opp_gaviota=on gives the defender the same
// tables, so it resists longest and N must shrink by exactly one.
#include "../lib/uci.hpp"
#include "syzygy_positions.hpp"
#include "game_rules.hpp"
#include "uci_engine.hpp"

#include <atomic>
#include <csignal>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

struct Suite_Entry
{
    std::string fen, name;
    int wdl = 0, dtz = 0;
};

static std::string result_text(const Suite_Entry& e)
{
    return e.fen + " hmvc 0; fmvn 1; wdl " + std::to_string(e.wdl) + "; dtz " + std::to_string(e.dtz) + "; c0 \"" + e.name + "\"";
}

static int make_suite(const std::string& dir, const std::string& out_path)
{
    if(syzygy::init(dir) == 0) { std::fprintf(stderr, "no tables in %s\n", dir.c_str()); return 2; }
    std::mt19937_64 rng(38);
    std::vector<Suite_Entry> won, drawn;

    // Random positions of `material` (the named side's pieces on white, then on
    // black) that probe as wdl, side to move being the named side. `hardest`:
    // keep the `count` with the largest |DTZ| out of `pool` candidates.
    auto collect = [&](std::vector<Suite_Entry>& into, const std::string& material, int wanted_wdl, int count, int pool)
    {
        int counts[12];
        tb_material(material, counts);
        std::vector<Suite_Entry> found;
        for(int tries = 0; (int)found.size() < pool && tries < 200*pool; tries++)
        {
            bool white = found.size() % 2 == 0;
            std::string fen = tb_random_fen(counts, !white, white, false, rng);
            if(fen.empty()) continue;
            BB pos;
            uci_parse_fen(fen, pos);
            int wdl, dtz;
            if(!syzygy::probe_wdl(&pos, wdl) || !syzygy::probe_dtz(&pos, dtz)) continue;
            if(wdl != wanted_wdl) continue;
            BB kids[MAX_LEGAL_MOVES];
            if(std::get<0>(all_moves(&pos, kids)) == 0) continue;
            found.push_back({fen.substr(0, fen.rfind(' ', fen.rfind(' ')-1)), material, wdl, dtz});
        }
        if(pool > count)
        std::stable_sort(found.begin(), found.end(), [](const Suite_Entry& a, const Suite_Entry& b) { return std::abs(a.dtz) > std::abs(b.dtz); });
        for(int i = 0; i < count && i < (int)found.size(); i++)
        into.push_back(found[i]);
    };
    collect(won, "KQvK", 2, 3, 3);
    collect(won, "KRvK", 2, 4, 4);
    collect(won, "KBBvK", 2, 4, 4);
    collect(won, "KBNvK", 2, 8, 4000);   // the hardest starts
    collect(won, "KPvK", 2, 3, 3);
    collect(won, "KQvKR", 2, 4, 4);
    collect(won, "KQvKB", 2, 2, 2);
    collect(won, "KRPvKR", 2, 5, 5);
    collect(won, "KQPvKQ", 2, 3, 3);
    collect(won, "KRvKN", 2, 2, 2);
    collect(drawn, "KRvKB", 0, 5, 5);
    collect(drawn, "KRvKN", 0, 3, 3);
    collect(drawn, "KRPvKR", 0, 6, 6);
    collect(drawn, "KBPvK", 0, 3, 3);
    collect(drawn, "KPvKP", 0, 3, 3);
    collect(drawn, "KNNvK", 0, 2, 2);
    std::ofstream f(out_path);
    f << "# OWNERSHIP=Claude\n# Tablebase endgame suite for tools/tb_suite, made by `tools/tb_suite make` (seed 38).\n"
         "# wdl 2: the side to move wins (dtz = plies to the next zeroing move); wdl 0: draw.\n";
    for(const Suite_Entry& e : won) f << result_text(e) << "\n";
    for(const Suite_Entry& e : drawn) f << result_text(e) << "\n";
    std::printf("%zu won, %zu drawn positions -> %s\n", won.size(), drawn.size(), out_path.c_str());
    return 0;
}

static std::vector<Suite_Entry> load_suite(const std::string& path)
{
    std::ifstream f(path);
    if(!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    std::vector<Suite_Entry> out;
    std::string line;
    while(std::getline(f, line))
    {
        if(line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        Suite_Entry e;
        std::string field;
        for(int i=0; i<4 && in >> field; i++) e.fen += (i ? " " : "") + field;
        std::string ops;
        std::getline(in, ops);
        auto number = [&](const std::string& name) { size_t at = ops.find(" " + name + " "); return at == std::string::npos ? 0 : std::atoi(ops.c_str() + at + name.size() + 2); };
        e.wdl = number("wdl");
        e.dtz = number("dtz");
        size_t c = ops.find("c0 \"");
        if(c != std::string::npos) e.name = ops.substr(c+4, ops.find('"', c+4)-c-4);
        BB check;
        if(!uci_parse_fen(e.fen + " 0 1", check)) { std::fprintf(stderr, "bad position: %s\n", line.c_str()); std::exit(2); }
        out.push_back(e);
    }
    return out;
}

struct Game_Result
{
    bool pass = false;
    std::string outcome;   // "wins" / "draws" / "loses" from the tested engine's side + reason
    int plies = 0;
    std::string pgn;
    std::string mates;     // the tested engine's mate N, move by move (gaviota=)
};

// N of a "score mate N" in an info line, 0 if it has none.
static int reported_mate(const std::string& info)
{
    size_t at = info.find(" score mate ");
    return at == std::string::npos ? 0 : std::atoi(info.c_str() + at + 12);
}

// mate_check: 0 off, 1 the tested engine's mate N shrinks every move, 2 by exactly one.
static Game_Result play(Engine& tested, Engine& opponent, const Suite_Entry& e, bool tested_white, long long movetime, long long opp_movetime, int mate_check)
{
    int last_mate = 0;
    std::string mate_fail;
    Game_Result r;
    Game* game = new Game;
    BB start;
    uci_parse_fen(e.fen + " 0 1", start);
    game->start(start, 0, 1);
    for(Engine* eng : {&tested, &opponent})
    {
        eng->send("ucinewgame");
        if(!eng->ready() && !eng->restart()) { r.outcome = "engine does not respond"; delete game; return r; }
    }
    std::string reason;
    Outcome o;
    while((o = game->outcome(reason)) == Outcome::ONGOING && game->uci_moves.size() < 600)
    {
        bool wtm = game->positions.back().white_move;
        bool is_tested = wtm == tested_white;
        Engine& eng = is_tested ? tested : opponent;
        std::string position = "position fen " + e.fen + " 0 1";
        if(!game->uci_moves.empty()) position += " moves";
        for(const std::string& m : game->uci_moves) position += " " + m;
        eng.send(position);
        eng.last_info.clear();
        eng.send("go movetime " + std::to_string(is_tested ? movetime : opp_movetime));
        std::string line;
        if(!eng.wait_for("bestmove", 60000 + movetime, &line)) { reason = "engine hung"; o = Outcome::DRAW; break; }
        std::istringstream in(line);
        std::string bm, move;
        in >> bm >> move;
        if(is_tested && mate_check && e.wdl == 2 && mate_fail.empty())
        {
            const int n = reported_mate(eng.last_info);
            r.mates += (r.mates.empty() ? "" : " ") + std::to_string(n);
            if(n <= 0) mate_fail = "no mate reported";
            else if(last_mate && (mate_check == 2 ? n != last_mate - 1 : n >= last_mate)) mate_fail = "mate " + std::to_string(last_mate) + " then " + std::to_string(n);
            last_mate = n;
        }
        if(!game->play(move)) { reason = std::string(is_tested ? "tested" : "opponent") + " illegal move " + move; o = is_tested ? (tested_white ? Outcome::BLACK_WINS : Outcome::WHITE_WINS) : Outcome::DRAW; break; }
    }
    r.plies = (int)game->uci_moves.size();
    if(o == Outcome::ONGOING) { reason = "too long"; o = Outcome::DRAW; }
    const bool tested_won = (o == Outcome::WHITE_WINS && tested_white) || (o == Outcome::BLACK_WINS && !tested_white);
    const bool tested_lost = (o == Outcome::WHITE_WINS && !tested_white) || (o == Outcome::BLACK_WINS && tested_white);
    r.outcome = std::string(tested_won ? "wins" : tested_lost ? "loses" : "draws") + " (" + reason + ")";
    if(tested_won && mate_check && last_mate != 1 && mate_fail.empty()) mate_fail = "mated after mate " + std::to_string(last_mate);
    r.pass = e.wdl == 2 ? tested_won && mate_fail.empty() : !tested_lost;
    if(!mate_fail.empty()) r.outcome += ", " + mate_fail;
    std::string pgn = "[Event \"tb_suite\"]\n[White \"" + std::string(tested_white ? "tested" : "opponent") + "\"]\n[Black \"" + std::string(tested_white ? "opponent" : "tested") + "\"]\n[FEN \"" + e.fen + " 0 1\"]\n[SetUp \"1\"]\n[Result \"" + (o == Outcome::WHITE_WINS ? "1-0" : o == Outcome::BLACK_WINS ? "0-1" : "1/2-1/2") + "\"]\n\n";
    bool white_first = game->positions[0].white_move;
    for(size_t i = 0; i < game->san_moves.size(); i++)
    {
        bool white = (i % 2 == 0) == white_first;
        if(white) pgn += std::to_string(1 + ((int)i + !white_first) / 2) + ". ";
        else if(i == 0) pgn += "1... ";
        pgn += game->san_moves[i] + " ";
    }
    r.pgn = pgn + "\n\n";
    delete game;
    return r;
}

int main(int argc, char** argv)
{
    if(argc < 3)
    {
        std::fprintf(stderr, "usage: %s make <tables dir> <out.epd>\n       %s run <tables dir> [suite=] [engine=] [movetime=1000] [opp_movetime=100] [concurrency=n] [tables=on|off] [pgn=] [gaviota=<dir>] [opp_gaviota=on]\n", argv[0], argv[0]);
        return 2;
    }
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    signal(SIGPIPE, SIG_IGN);
    const std::string mode = argv[1], dir = argv[2];
    if(mode == "make")
    return argc > 3 ? make_suite(dir, argv[3]) : 2;
    if(mode != "run") return 2;

    std::map<std::string, std::string> opt;
    for(int i = 3; i < argc; i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq == std::string::npos || eq == 0) { std::fprintf(stderr, "expected key=value, got %s\n", a.c_str()); return 2; }
        if(eq + 1 < a.size()) opt[a.substr(0, eq)] = a.substr(eq + 1);
    }
    auto get = [&](const std::string& k, const std::string& def) { return opt.count(k) ? opt[k] : def; };
    const std::vector<Suite_Entry> suite = load_suite(get("suite", "tools/tb_suite.epd"));
    const std::string engine_path = get("engine", "./ascaniusfish_uci");
    const long long movetime = std::atoll(get("movetime", "1000").c_str());
    const long long opp_movetime = std::atoll(get("opp_movetime", "100").c_str());
    const bool tables_on = get("tables", "on") != "off";
    const std::string gaviota_dir = get("gaviota", "");
    const bool opp_gaviota = get("opp_gaviota", "off") == "on";
    const int mate_check = gaviota_dir.empty() ? 0 : opp_gaviota ? 2 : 1;
    const int cores = std::max(1u, std::thread::hardware_concurrency());
    const int concurrency = std::max(1, std::atoi(get("concurrency", std::to_string(std::min(2, std::max(1, cores/2)))).c_str()));

    struct Job { int entry; bool tested_white; };
    std::vector<Job> jobs;
    for(int i = 0; i < (int)suite.size(); i++)
    {
        BB pos;
        uci_parse_fen(suite[i].fen + " 0 1", pos);
        if(suite[i].wdl == 2) jobs.push_back({i, pos.white_move});   // the side to move wins
        else { jobs.push_back({i, true}); jobs.push_back({i, false}); }
    }
    std::vector<Game_Result> results(jobs.size());
    std::atomic<int> next(0);
    std::mutex mutex;
    auto worker = [&]()
    {
        Engine tested, opponent;
        tested.path = opponent.path = engine_path;
        if(tables_on) tested.options.push_back({"SyzygyPath", dir});
        if(!gaviota_dir.empty()) tested.options.push_back({"GaviotaTbPath", gaviota_dir});
        if(!gaviota_dir.empty() && opp_gaviota) opponent.options.push_back({"GaviotaTbPath", gaviota_dir});
        if(!tested.start() || !opponent.start()) { std::fprintf(stderr, "engine does not start: %s\n", engine_path.c_str()); std::exit(2); }
        for(int j; (j = next++) < (int)jobs.size(); )
        {
            const Suite_Entry& e = suite[jobs[j].entry];
            results[j] = play(tested, opponent, e, jobs[j].tested_white, movetime, opp_movetime, mate_check);
            std::lock_guard<std::mutex> lock(mutex);
            std::printf("%-7s %-5s %-4s %s  %s %d plies  %s\n", e.name.c_str(), e.wdl == 2 ? "won" : "drawn", jobs[j].tested_white ? "as W" : "as B", e.fen.c_str(), results[j].outcome.c_str(), results[j].plies, results[j].pass ? "PASS" : "FAIL");
            if(!results[j].mates.empty()) std::printf("        mate: %s\n", results[j].mates.c_str());
            std::fflush(stdout);
        }
        tested.stop();
        opponent.stop();
    };
    std::vector<std::thread> threads;
    for(int w = 0; w < concurrency; w++) threads.emplace_back(worker);
    for(std::thread& t : threads) t.join();

    std::ofstream pgn(get("pgn", "tb_suite.pgn"));
    int won_games = 0, won_pass = 0, drawn_games = 0, drawn_pass = 0;
    for(size_t j = 0; j < jobs.size(); j++)
    {
        pgn << results[j].pgn;
        (suite[jobs[j].entry].wdl == 2 ? won_games : drawn_games)++;
        (suite[jobs[j].entry].wdl == 2 ? won_pass : drawn_pass) += results[j].pass;
    }
    std::printf("\nwon positions:   %d/%d won by the engine\ndrawn positions: %d/%d not lost\n", won_pass, won_games, drawn_pass, drawn_games);
    return won_pass == won_games && drawn_pass == drawn_games ? 0 : 1;
}
