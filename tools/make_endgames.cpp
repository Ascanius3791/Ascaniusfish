// OWNERSHIP=Claude
// Builds the endgame match suite (tools/endgames.epd), issue #41. One-off: the
// result is committed, this is kept so it can be rebuilt or extended.
//
//   ./tools/make_endgames <out.epd> [count=120] [engine=./ascaniusfish_uci] [judge=~/stockfish15/src/stockfish]
//                         [openings=tools/openings.epd] [depth=4] [judge_depth=18] [concurrency=3]
//
// Positions come from the engine's own self-play: every opening of the match
// suite is played on by `engine` at a fixed depth, after a few random plies (a
// fixed seed) so the runs differ, until a capture leaves a random 6, 7 or 8
// pieces on the board. `judge` (Stockfish) then searches the position, and it
// is kept if |cp| <= MAX_CP and it is no forced mate. The Lichess cloud eval
// used for the openings has almost nothing on such positions, so a local judge
// takes its place. One position per distinct piece placement.
#include "game_rules.hpp"
#include "uci_engine.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>

constexpr int MIN_PIECES = 6, MAX_PIECES = 8;
constexpr int MAX_CP = 150, RANDOM_PLIES = 2, MAX_GAME_PLIES = 240;
constexpr int RUNS = 32;
constexpr int SEED = 20260930;

static int piece_total(const BB& pos)
{
    int n = 0;
    for(int i=0;i<12;i++)
    n += __builtin_popcountll(pos.Board[i]);
    return n;
}

struct Candidate { std::string fen, epd, source; };

static std::string arg(int argc, char** argv, const std::string& key, const std::string& fallback)
{
    for(int i=2;i<argc;i++)
    if(std::string(argv[i]).compare(0, key.size()+1, key+"=")==0) return argv[i]+key.size()+1;
    return fallback;
}

// One self-play game from `fen`; true with the position that has `target` pieces.
static bool play_to_endgame(Engine& e, const std::string& fen, int target, int depth, std::mt19937& rng, Game& game)
{
    BB start;
    uci_parse_fen(fen, start);
    game.start(start, 0, 1);
    e.send("ucinewgame");
    if(!e.ready()) return false;
    std::string moves;
    for(int ply=0; ply<MAX_GAME_PLIES; ply++)
    {
        std::string why;
        if(game.outcome(why)!=Outcome::ONGOING) return false;
        std::string move;
        if(ply<RANDOM_PLIES)
        move = get_UCI(&game.positions.back(), game.children + rng()%game.n_children);
        else
        {
            e.send("position fen " + fen + (moves.empty() ? "" : " moves" + moves));
            e.send("go depth " + std::to_string(depth));
            std::string line;
            if(!e.wait_for("bestmove", 120000, &line)) return false;
            move = line.substr(9, line.find(' ', 9)-9);
        }
        if(!game.play(move)) return false;
        moves += " " + move;
        if(piece_total(game.positions.back())<=target) return piece_total(game.positions.back())==target;
    }
    return false;
}

// Judge's score in white's view (cp); false on a mate score.
static bool judge_eval(Engine& j, const std::string& fen, int depth, int& cp)
{
    j.send("position fen " + fen);
    j.send("go depth " + std::to_string(depth));
    if(!j.wait_for("bestmove", 120000)) return false;
    Search_Info s;
    if(!parse_info(j.last_info, s) || s.score_kind!="cp") return false;
    cp = std::atoi(s.score_value.c_str());
    if(fen.find(" b ")!=std::string::npos) cp = -cp;
    return true;
}

int main(int argc, char** argv)
{
    if(argc<2)
    {
        std::fprintf(stderr, "usage: %s <out.epd> [count=120] [engine=...] [judge=...] [openings=...] [depth=4] [judge_depth=18] [concurrency=3]\n", argv[0]);
        return 2;
    }
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    const int count = std::atoi(arg(argc, argv, "count", "120").c_str());
    const int depth = std::atoi(arg(argc, argv, "depth", "4").c_str());
    const int judge_depth = std::atoi(arg(argc, argv, "judge_depth", "18").c_str());
    const int workers = std::atoi(arg(argc, argv, "concurrency", "3").c_str());
    const std::string engine_path = arg(argc, argv, "engine", "./ascaniusfish_uci");
    const std::string judge_path = arg(argc, argv, "judge", std::string(getenv("HOME")) + "/stockfish15/src/stockfish");

    struct Opening { std::string fen, name; };
    std::vector<Opening> openings;
    {
        std::ifstream in(arg(argc, argv, "openings", "tools/openings.epd"));
        std::string row;
        while(std::getline(in, row))
        {
            if(row.empty() || row[0]=='#') continue;
            size_t h = row.find(" hmvc "), f = row.find("fmvn "), c = row.find("c0 \"");
            if(h==std::string::npos || f==std::string::npos) continue;
            std::string fen = row.substr(0, h) + " " + std::to_string(std::atoi(row.c_str()+h+6)) + " " + std::to_string(std::atoi(row.c_str()+f+5));
            std::string name = c==std::string::npos ? "" : row.substr(c+4, row.find('"', c+4)-c-4);
            openings.push_back({fen, name});
        }
    }
    std::printf("%zu openings, judge %s depth %d\n", openings.size(), judge_path.c_str(), judge_depth);

    // RUNS runs per opening, each aimed at its own piece count (8, 7, 6 in turn).
    std::vector<std::pair<int, int>> jobs;  // (opening, run)
    for(int run=0; run<RUNS; run++)
    for(size_t o=0;o<openings.size();o++) jobs.push_back({(int)o, run});

    std::mutex mu;
    std::atomic<size_t> next_job{0};
    std::vector<Candidate> kept;
    std::set<std::string> placements;
    std::map<int, int> by_pieces;
    int games = 0, judged = 0, too_lopsided = 0, duplicates = 0;

    auto worker = [&](int id)
    {
        Engine e, j;
        e.path = engine_path;
        e.options = {{"Hash", "16"}};
        j.path = judge_path;
        j.options = {{"Threads", "1"}, {"Hash", "64"}};
        if(!e.start() || !j.start()) { std::fprintf(stderr, "cannot start engines\n"); return; }
        for(;;)
        {
            {
                std::lock_guard<std::mutex> lk(mu);
                if((int)kept.size()>=count) break;
            }
            size_t k = next_job++;
            if(k>=jobs.size()) break;
            auto [o, run] = jobs[k];
            std::mt19937 rng(SEED + 1000*run + o);
            int target = MAX_PIECES - run%3;
            Game game;
            bool ok = play_to_endgame(e, openings[o].fen, target, depth, rng, game);
            std::string fen = game.fen();
            std::string epd = fen.substr(0, fen.rfind(' ', fen.rfind(' ')-1));
            int cp = 0;
            bool good = ok && judge_eval(j, fen, judge_depth, cp) && std::abs(cp)<=MAX_CP;
            std::lock_guard<std::mutex> lk(mu);
            games++;
            if(ok) judged++;
            if(ok && !good) too_lopsided++;
            if(good && placements.count(epd)) duplicates++;
            if(good && !placements.count(epd) && (int)kept.size()<count)
            {
                placements.insert(epd);
                by_pieces[target]++;
                int ce = game.positions.back().white_move ? cp : -cp;
                std::string moves;
                for(const std::string& m : game.uci_moves) moves += (moves.empty() ? "" : " ") + m;
                kept.push_back({fen, epd + " hmvc " + std::to_string(game.halfmove_clock) + "; fmvn " + std::to_string(game.fullmove())
                                + "; ce " + std::to_string(ce) + "; acd " + std::to_string(judge_depth) + "; c0 \"" + std::to_string(target)
                                + " pieces, from " + openings[o].name + "\"; c1 \"" + moves + "\";", epd});
                std::printf("%3zu (game %d, worker %d) %d pieces cp %+d  %s\n", kept.size(), games, id, target, cp, fen.c_str());
                std::fflush(stdout);
            }
        }
        e.stop();
        j.stop();
    };
    std::vector<std::thread> threads;
    for(int i=0;i<workers;i++) threads.emplace_back(worker, i);
    for(auto& t : threads) t.join();

    std::sort(kept.begin(), kept.end(), [](const Candidate& a, const Candidate& b) { return a.source<b.source; });
    std::ofstream f(argv[1]);
    f << "# OWNERSHIP=Claude\n"
      << "# Endgame match suite, built by tools/make_endgames.cpp: self-play of the engine from tools/openings.epd\n"
      << "# until " << MIN_PIECES << "-" << MAX_PIECES << " pieces are left, kept when the judge's |cp| <= " << MAX_CP
      << " (ce: side to move, acd: judge depth).\n";
    for(const Candidate& c : kept) f << c.epd << "\n";
    std::printf("%d games, %d reached the target, %d judged out, %d duplicates; wrote %zu positions to %s (8: %d, 7: %d, 6: %d)\n",
                games, judged, too_lopsided, duplicates, kept.size(), argv[1], by_pieces[8], by_pieces[7], by_pieces[6]);
    return (int)kept.size()<count;
}
