// OWNERSHIP=Claude
// How long is the root PV when TT entries keep only their best move (#82)?
// Runs "go depth D" on the 130 positions (bench + tools/openings.epd) with three
// engines side by side, one thread each:
//   today        full=<binary>     (a TT entry keeps the whole line)
//   move only    compact=<binary>  (a build with lib/tt_result.hpp's TT_Result)
//   move + walk  the compact binary with TTWalk on
// and compares each iteration's PV at the depths asked for against today's.
// The search is the same in all three, so nodes, score and first move must
// agree; a mismatch is printed and counted.
//
//   diagnostics/root_pv_length full=<uci binary> compact=<uci binary> [depths=8,9,10] [n=130] [rows=<file>]
//
// Build from the repo root:
//   g++ -O2 -pthread -o diagnostics/root_pv_length diagnostics/root_pv_length.cpp
#include "../tools/uci_engine.hpp"
#include "../tools/bench_positions.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

constexpr int TM_PV_PLIES = 24;  // what the time manager compares (TM_PV_LEN, lib/time_manager.hpp)

struct Iteration
{
    bool seen = false;
    long long nodes = 0;
    std::string score;
    std::vector<std::string> pv;
};

// result[position][depth]
using Results = std::vector<std::map<int, Iteration>>;

static bool run_engine(const std::string& path, bool walk, const std::vector<std::string>& fens, int max_depth, Results& out, std::string& error)
{
    Engine engine;
    engine.path = path;
    if(walk)
    engine.options.push_back({"TTWalk", "true"});
    if(!engine.start())
    {
        error = "cannot start " + path;
        return false;
    }
    out.assign(fens.size(), {});
    for(size_t k=0; k<fens.size(); k++)
    {
        engine.send("ucinewgame");
        if(!engine.ready())
        {
            error = path + " not ready";
            return false;
        }
        engine.send("position fen " + fens[k]);
        engine.send("go depth " + std::to_string(max_depth));
        std::string line;
        bool done = false;
        while(!done && engine.read_line(line, now_ms()+3600000))
        {
            Search_Info info;
            if(parse_info(line, info))
            {
                Iteration& it = out[k][info.depth];
                it.seen = true;
                it.nodes = info.nodes;
                it.score = info.score_kind + " " + info.score_value;
                it.pv = info.pv;
            }
            else if(line.compare(0, 9, "bestmove ")==0)
            done = true;
        }
        if(!done)
        {
            error = path + " gave no bestmove";
            return false;
        }
    }
    engine.send("quit");
    return true;
}

static size_t common_prefix(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    size_t i = 0;
    while(i<a.size() && i<b.size() && a[i]==b[i])
    i++;
    return i;
}

static std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for(const std::string& m : v)
    s += (s.empty() ? "" : " ") + m;
    return s;
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
    if(!opt.count("full") || !opt.count("compact"))
    {
        std::cerr << "usage: root_pv_length full=<uci binary> compact=<uci binary> [depths=8,9,10] [n=130] [rows=<file>]\n";
        return 2;
    }
    std::vector<int> depths;
    {
        std::istringstream in(get("depths", "8,9,10"));
        std::string d;
        while(std::getline(in, d, ','))
        depths.push_back(std::atoi(d.c_str()));
    }
    int max_depth = 0;
    for(int d : depths)
    max_depth = std::max(max_depth, d);

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
    const size_t n = std::min(fens.size(), (size_t)std::atoi(get("n", "130").c_str()));
    fens.resize(n);

    const char* names[3] = {"today", "move only", "move + walk"};
    Results results[3];
    std::string errors[3];
    bool ok[3];
    long long started = now_ms();
    std::thread threads[3];
    for(int v=0; v<3; v++)
    threads[v] = std::thread([&, v]
    {
        ok[v] = run_engine(v==0 ? get("full", "") : get("compact", ""), v==2, fens, max_depth, results[v], errors[v]);
    });
    for(std::thread& t : threads)
    t.join();
    for(int v=0; v<3; v++)
    if(!ok[v])
    {
        std::cerr << errors[v] << "\n";
        return 1;
    }

    std::ofstream rows_out;
    if(opt.count("rows"))
    rows_out.open(get("rows", ""));
    std::printf("%zu positions, go depth %d, %.0f s\n\n", n, max_depth, (now_ms()-started)/1000.0);
    std::printf("%-5s %-12s %5s %8s %8s %8s %9s %9s %9s %10s\n",
                "depth", "variant", "lines", "mean len", "min(24)", "<24", "identical", "on today", "differ", "missing");
    int mismatches = 0;
    for(int d : depths)
    {
        for(int v=0; v<3; v++)
        {
            int lines = 0, identical = 0, short_lines = 0;
            long long len = 0, len24 = 0, on_today = 0, differ = 0, missing = 0;
            for(size_t k=0; k<n; k++)
            {
                const Iteration& t = results[0][k][d];
                const Iteration& it = results[v][k][d];
                if(!t.seen || !it.seen)
                continue;
                if(it.nodes!=t.nodes || it.score!=t.score || (it.pv.empty() ? "" : it.pv[0])!=(t.pv.empty() ? "" : t.pv[0]))
                {
                    mismatches++;
                    std::printf("MISMATCH depth %d %s #%zu: nodes %lld/%lld score %s/%s\n", d, names[v], k+1,
                                it.nodes, t.nodes, it.score.c_str(), t.score.c_str());
                }
                lines++;
                const size_t c = common_prefix(it.pv, t.pv);
                len += it.pv.size();
                len24 += std::min((int)it.pv.size(), TM_PV_PLIES);
                short_lines += (int)it.pv.size()<TM_PV_PLIES;
                identical += it.pv==t.pv;
                on_today += c;
                differ += it.pv.size()-c;
                missing += t.pv.size()-c;
                if(rows_out)
                rows_out << d << '\t' << names[v] << '\t' << (k+1) << '\t' << it.pv.size() << '\t' << c << '\t' << join(it.pv) << '\n';
            }
            const double L = std::max(1, lines);
            std::printf("%-5d %-12s %5d %8.2f %8.2f %8d %9d %9.2f %9.2f %10.2f\n",
                        d, names[v], lines, len/L, len24/L, short_lines, identical, on_today/L, differ/L, missing/L);
        }
        std::printf("\n");
    }
    std::printf("mean len: plies in the root PV; min(24): what the time manager compares; <24: lines shorter than that.\n");
    std::printf("Against today's line, per line: on today = plies before the first difference, differ = plies after it\n");
    std::printf("(moves today's line does not have there), missing = today's plies after it.\n");
    std::printf("mismatches (nodes, score or first move): %d\n", mismatches);
    return mismatches ? 1 : 0;
}
