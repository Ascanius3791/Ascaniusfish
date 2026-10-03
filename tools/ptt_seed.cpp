// OWNERSHIP=Claude
// Does a stored line make the next search faster (#79)? For each position: a
// search of store_ms (what the PTT keeps: depth D and its line), then from an
// empty TT once cold and once with "hint depth D pv <line>", both to depth D+1.
// Prints the time and nodes each needed to finish D+1, and the totals.
//
//   tools/ptt_seed [engine=./ascaniusfish_uci] [n=30] [store_ms=5000] [nne=true]
//
// Positions: tools/bench_positions.hpp's, then tools/openings.epd's, skipping
// any whose stored search ends before store_ms (a position solved outright
// has nothing deeper to reach).
#include "uci_engine.hpp"
#include "bench_positions.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>

struct Run
{
    bool ok = false;
    long long time_ms = 0, nodes = 0;   // of the depth D+1 iteration
    std::string score, best;
};

static std::string score_of(const Search_Info& info)
{
    return info.score_kind + " " + info.score_value;
}

// Searches `fen` from an empty TT to `depth`, after `hint` if not empty.
static Run search_to(Engine& engine, const std::string& fen, int depth, const std::string& hint)
{
    Run run;
    engine.send("ucinewgame");
    if(!engine.ready())
    return run;
    engine.send("position fen " + fen);
    if(!hint.empty())
    engine.send(hint);
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
            run.ok = last.depth==depth;
            run.time_ms = last.time_ms;
            run.nodes = last.nodes;
            run.score = score_of(last);
            run.best = line.substr(9, line.find(' ', 9)-9);
            return run;
        }
    }
    return run;
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
    const int want = std::atoi(get("n", "30").c_str());
    const long long store_ms = std::atoll(get("store_ms", "5000").c_str());

    std::vector<std::string> fens(std::begin(BENCH_FENS), std::end(BENCH_FENS));
    std::ifstream epd("tools/openings.epd");
    std::string row;
    while(std::getline(epd, row))
    {
        if(row.empty() || row[0]=='#')
        continue;
        std::istringstream in(row);
        std::string f[4];
        in >> f[0] >> f[1] >> f[2] >> f[3];
        fens.push_back(f[0]+" "+f[1]+" "+f[2]+" "+f[3]+" 0 1");
    }

    Engine engine;
    engine.path = get("engine", "./ascaniusfish_uci");
    engine.options.push_back({"UseNNE", get("nne", "true")});
    if(!engine.start())
    {
        std::cerr << "cannot start " << engine.path << "\n";
        return 1;
    }
    std::printf("%-3s %-3s %9s %9s %6s %11s %11s %6s  %s\n", "#", "D", "cold ms", "seed ms", "t s/c", "cold nodes", "seed nodes", "n s/c", "scores cold | seeded");
    int done = 0;
    double log_t = 0, log_n = 0;
    long long sum_cold_t = 0, sum_seed_t = 0, sum_cold_n = 0, sum_seed_n = 0;
    int seed_faster = 0, same_score = 0, same_move = 0;
    for(size_t k=0; k<fens.size() && done<want; k++)
    {
        const std::string& fen = fens[k];
        // The stored search: what a >= store_ms analysis leaves in the PTT.
        engine.send("ucinewgame");
        engine.ready();
        engine.send("position fen " + fen);
        engine.send("go movetime " + std::to_string(store_ms));
        std::string line;
        Search_Info stored;
        long long started = now_ms();
        bool got = false;
        while(engine.read_line(line, now_ms()+store_ms+60000))
        {
            Search_Info info;
            if(parse_info(line, info))
            stored = info;
            else if(line.compare(0, 9, "bestmove ")==0)
            {
                got = true;
                break;
            }
        }
        if(!got || now_ms()-started<store_ms-200 || stored.depth<2 || stored.pv.size()<2)
        {
            std::fprintf(stderr, "skip %s (depth %d, %lld ms)\n", fen.c_str(), stored.depth, now_ms()-started);
            continue;
        }
        const int D = stored.depth;
        std::string hint = "hint depth " + std::to_string(D) + " pv";
        for(const std::string& m : stored.pv)
        hint += " " + m;
        // Alternate the order, so a warm cache or clock drift favours neither.
        Run cold, seeded;
        if(done%2==0)
        {
            cold = search_to(engine, fen, D+1, "");
            seeded = search_to(engine, fen, D+1, hint);
        }
        else
        {
            seeded = search_to(engine, fen, D+1, hint);
            cold = search_to(engine, fen, D+1, "");
        }
        if(!cold.ok || !seeded.ok)
        {
            std::fprintf(stderr, "skip %s (depth %d not reached)\n", fen.c_str(), D+1);
            continue;
        }
        done++;
        const double rt = (double)std::max(1LL, seeded.time_ms)/std::max(1LL, cold.time_ms);
        const double rn = (double)std::max(1LL, seeded.nodes)/std::max(1LL, cold.nodes);
        log_t += std::log(rt);
        log_n += std::log(rn);
        sum_cold_t += cold.time_ms;
        sum_seed_t += seeded.time_ms;
        sum_cold_n += cold.nodes;
        sum_seed_n += seeded.nodes;
        seed_faster += seeded.time_ms<cold.time_ms;
        same_score += cold.score==seeded.score;
        same_move += cold.best==seeded.best;
        std::printf("%-3d %-3d %9lld %9lld %6.2f %11lld %11lld %6.2f  %s %s | %s %s\n", done, D, cold.time_ms, seeded.time_ms, rt,
                    cold.nodes, seeded.nodes, rn, cold.score.c_str(), cold.best.c_str(), seeded.score.c_str(), seeded.best.c_str());
        std::fflush(stdout);
    }
    engine.stop();
    if(!done)
    return 1;
    std::printf("\npositions %d, seeded faster in %d\n", done, seed_faster);
    std::printf("total time  cold %lld ms, seeded %lld ms (%.3f)\n", sum_cold_t, sum_seed_t, (double)sum_seed_t/sum_cold_t);
    std::printf("total nodes cold %lld, seeded %lld (%.3f)\n", sum_cold_n, sum_seed_n, (double)sum_seed_n/sum_cold_n);
    std::printf("geometric mean seeded/cold: time %.3f, nodes %.3f\n", std::exp(log_t/done), std::exp(log_n/done));
    std::printf("same score at D+1: %d/%d, same move: %d/%d\n", same_score, done, same_move, done);
    return 0;
}
