// OWNERSHIP=Claude
// Does a shared TT make a fixed-depth Watch game faster (#91)? Plays games from
// the opening suite at a fixed depth and searches every position twice: once
// on one process that plays both sides (the GUI's Pure watch) and once on that
// side's own process (tournament mode, one per side). Both get what the GUI's
// Watch sends: one "ucinewgame" per process per game, then "position fen ...
// moves ..." and "go depth D". The game follows the shared engine's move in
// even games and the side's own in odd ones, so neither mode always plays its
// own game. Prints the wall time and nodes of every search, and per mode the
// totals.
//
//   tools/watch_tt [engine=./ascaniusfish_uci] [depth=9] [games=10] [plies=60] [nne=true]
//
// Openings: the first `games` of tools/openings.epd. A game stops after
// `plies` moves or when it ends by the rules.
#include "game_rules.hpp"
#include "uci_engine.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>

struct Run
{
    long long wall_ms = 0, nodes = 0;
    std::string best;
};

// One "go depth": the wall time from "go" to "bestmove", what a Watch move waits.
static Run search(Engine& engine, const std::string& position, int depth)
{
    Run run;
    engine.send(position);
    long long started = now_ms();
    engine.send("go depth " + std::to_string(depth));
    std::string line;
    Search_Info last;
    while(engine.read_line(line, now_ms()+3600000))
    {
        Search_Info info;
        if(parse_info(line, info))
        last = info;
        else if(line.compare(0, 9, "bestmove ")==0)
        {
            run.wall_ms = now_ms()-started;
            run.nodes = last.nodes;
            run.best = line.substr(9, line.find(' ', 9)-9);
            return run;
        }
    }
    std::fprintf(stderr, "watch_tt: %s gave no bestmove\n", engine.path.c_str());
    std::exit(2);
}

struct Interval { double mean, lo, hi; };

// The geometric mean of shared/separate and its 95% interval (log-normal).
static Interval geometric(const std::vector<double>& ratios)
{
    double sum = 0, sq = 0;
    for(double r : ratios) sum += std::log(r);
    const double n = ratios.size(), m = sum/n;
    for(double r : ratios) sq += (std::log(r)-m)*(std::log(r)-m);
    const double se = n>1 ? std::sqrt(sq/(n-1)/n) : 0;
    return {std::exp(m), std::exp(m-1.96*se), std::exp(m+1.96*se)};
}

int main(int argc, char** argv)
{
    std::map<std::string, std::string> opt;
    for(int i=1;i<argc;i++)
    {
        const char* eq = std::strchr(argv[i], '=');
        if(eq)
        opt[std::string(argv[i], eq-argv[i])] = eq+1;
    }
    auto get = [&](const std::string& k, const std::string& d) { return opt.count(k) ? opt[k] : d; };
    const int depth = std::atoi(get("depth", "9").c_str());
    const int games = std::atoi(get("games", "10").c_str());
    const int plies = std::atoi(get("plies", "60").c_str());

    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    signal(SIGPIPE, SIG_IGN);

    std::vector<std::string> fens;
    std::ifstream epd("tools/openings.epd");
    std::string row;
    while(std::getline(epd, row) && (int)fens.size()<games)
    {
        if(row.empty() || row[0]=='#')
        continue;
        std::istringstream in(row);
        std::string f[4];
        in >> f[0] >> f[1] >> f[2] >> f[3];
        fens.push_back(f[0]+" "+f[1]+" "+f[2]+" "+f[3]+" 0 1");
    }

    // [0] shared, [1] white's own, [2] black's own.
    Engine engines[3];
    for(Engine& e : engines)
    {
        e.path = get("engine", "./ascaniusfish_uci");
        e.options.push_back({"UseNNE", get("nne", "true")});
        e.options.push_back({"MultiPV", "1"});
        if(!e.start())
        {
            std::cerr << "cannot start " << e.path << "\n";
            return 1;
        }
    }

    std::printf("%-4s %-5s %9s %9s %6s %11s %11s %6s\n", "game", "ply", "shared ms", "own ms", "t s/o", "shared n", "own n", "n s/o");
    long long total_t[2] = {0, 0}, total_n[2] = {0, 0};
    std::vector<double> move_t, move_n, game_t;
    int same_move = 0;
    for(int g=0; g<(int)fens.size(); g++)
    {
        for(Engine& e : engines)
        {
            e.send("ucinewgame");
            e.ready();
        }
        std::unique_ptr<Game> game(new Game);
        BB start;
        uci_parse_fen(fens[g], start);
        game->start(start, 0, 1);
        long long game_total[2] = {0, 0};
        for(int ply=0; ply<plies; ply++)
        {
            std::string reason;
            if(game->outcome(reason)!=Outcome::ONGOING)
            break;
            std::string position = "position fen " + fens[g];
            if(!game->uci_moves.empty())
            {
                position += " moves";
                for(const std::string& m : game->uci_moves)
                position += " " + m;
            }
            Engine& own = engines[game->positions.back().white_move ? 1 : 2];
            // Alternate which goes first, so a warm cache favours neither.
            Run shared, separate;
            if(ply%2==0)
            {
                shared = search(engines[0], position, depth);
                separate = search(own, position, depth);
            }
            else
            {
                separate = search(own, position, depth);
                shared = search(engines[0], position, depth);
            }
            if(!game->play(g%2==0 ? shared.best : separate.best))
            {
                std::fprintf(stderr, "watch_tt: illegal move %s\n", (g%2==0 ? shared.best : separate.best).c_str());
                return 2;
            }
            total_t[0] += shared.wall_ms;   total_t[1] += separate.wall_ms;
            total_n[0] += shared.nodes;     total_n[1] += separate.nodes;
            game_total[0] += shared.wall_ms; game_total[1] += separate.wall_ms;
            const double rt = (double)std::max(1LL, shared.wall_ms)/std::max(1LL, separate.wall_ms);
            const double rn = (double)std::max(1LL, shared.nodes)/std::max(1LL, separate.nodes);
            move_t.push_back(rt);
            move_n.push_back(rn);
            same_move += shared.best==separate.best;
            std::printf("%-4d %-5d %9lld %9lld %6.2f %11lld %11lld %6.2f\n", g+1, ply+1, shared.wall_ms, separate.wall_ms, rt,
                        shared.nodes, separate.nodes, rn);
            std::fflush(stdout);
        }
        game_t.push_back((double)std::max(1LL, game_total[0])/std::max(1LL, game_total[1]));
        std::printf("game %d: %zu plies, shared %lld ms, own %lld ms (%.3f)\n", g+1, game->uci_moves.size(),
                    game_total[0], game_total[1], game_t.back());
    }
    for(Engine& e : engines)
    e.stop();
    const size_t n = move_t.size();
    if(!n)
    return 1;
    Interval t = geometric(move_t), nodes = geometric(move_n), per_game = geometric(game_t);
    std::printf("\ndepth %d, %zu games, %zu moves, same move %d/%zu\n", depth, game_t.size(), n, same_move, n);
    std::printf("time per move  shared %.0f ms, own %.0f ms (%.3f)\n", (double)total_t[0]/n, (double)total_t[1]/n,
                (double)total_t[0]/total_t[1]);
    std::printf("nodes per move shared %.0f, own %.0f (%.3f)\n", (double)total_n[0]/n, (double)total_n[1]/n,
                (double)total_n[0]/total_n[1]);
    std::printf("geometric mean shared/own per move: time %.3f (%.3f-%.3f), nodes %.3f (%.3f-%.3f)\n",
                t.mean, t.lo, t.hi, nodes.mean, nodes.lo, nodes.hi);
    std::printf("per game (total time): %.3f (%.3f-%.3f)\n", per_game.mean, per_game.lo, per_game.hi);
    return 0;
}
