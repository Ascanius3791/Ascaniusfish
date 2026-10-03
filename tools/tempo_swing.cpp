// OWNERSHIP=Claude
// tools/tempo_swing (#70): how much the static eval favours the side that just
// moved, measured from the root score's odd/even swing over depth.
//
//   ./tools/tempo_swing [engine=.] [positions=nets/nne_d6_test.tsv] [per_bin=600]
//                       [depths=2,4,6] [agree=30] [jobs=4] [minutes=10] [tt=17]
//                       [seed=70] [options=Name=Value,...] [out=<raw.tsv>]
//
// A leaf of an even-depth search has the root's mover to move, a leaf of an
// odd-depth one the opponent. If the eval gives the side that just moved T
// too much, s(odd) is too high by T and s(even) too low by T, in the root
// mover's view (UCI's cp). So for d even
//     T̂ = (s(d+1) - (s(d)+s(d+2))/2) / 2,
// half the swing. One "go depth max(depths)+2" per position gives every s.
//
// Positions: the FEN column of a tab-separated file (nne_data's layout, FEN in
// column 2; a line with one column is a FEN too), shuffled with `seed`, kept
// when quiet at the root: not in check, and no capture with SEE >= 0
// (lib/see.hpp's is_good_capture()) or queen promotion for the mover. They
// are binned by phase: both sides' material in 39ths, 0..78, as piecetable()
// counts it, in 6 bins of 13 (promoted material above 78 goes in the last).
// Each bin takes up to `per_bin`. At a depth d a position counts only when
// s(d), s(d+1) and s(d+2) are all cp scores and |s(d) - s(d+2)| <= agree:
// a root whose score is still moving says nothing about parity.
//
// Engines: `engine` is a UCI binary or a git ref built like tools/match does
// ("." = working tree, TT exponent `tt`). Each gets UseNNE=false, then
// `options`. `jobs` engines run at once; every position starts with
// ucinewgame. After `minutes` no new position is started.
//
// Output, per phase bin and per depth: the median T̂ with its 95% CI (order
// statistics) and n, then the fit T(p) = (T_o*p + T_e*(78-p))/78 over the bin
// medians (weighted by n), which is what basic_eval()'s tempo term uses.
// out= writes one line per position: fen, phase, s(1..dmax+2).
#include "../lib/uci.hpp"
#include "git_build.hpp"
#include "uci_engine.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <thread>

static const int BINS = 6;
static const int PHASE_MAX = 78;  // both sides' material in 39ths
static const int NO_SCORE = INT_MIN;

[[noreturn]] static void die(const std::string& msg)
{
    std::fprintf(stderr, "tempo_swing: %s\n", msg.c_str());
    std::exit(2);
}

static int phase_of(const BB& pos)
{
    int p = 0;
    for(int side=0;side<2;side++)
    p += 1*count(pos.Board[0+6*side]) + 5*count(pos.Board[1+6*side]) + 3*count(pos.Board[2+6*side])
       + 3*count(pos.Board[3+6*side]) + 9*count(pos.Board[4+6*side]);
    return p;
}

static int bin_of(int phase)
{
    return std::min(BINS-1, phase*BINS/(PHASE_MAX+1));
}

// Not in check, no capture that keeps material, no queen promotion.
static bool quiet_root(const BB& pos)
{
    if(pos.get_in_check())
    return false;
    Move_List list;
    generate_legal_moves(&pos, list, GEN_CAPTURES);
    for(int i=0;i<list.size;i++)
    {
        const Move& m = list[i];
        if(m.promotion_piece_type==QUEEN_PROMOTION)
        return false;
        if(is_capturing_move(pos.Board, m.to, pos.white_move, m.is_en_passant) && is_good_capture(&pos, m.from, m.to, m.is_en_passant))
        return false;
    }
    return true;
}

struct Sample
{
    std::string fen;
    int phase = 0;
    std::vector<int> score;  // [d] = s(d), mover's view cp; NO_SCORE when missing or a mate
};

static std::vector<std::pair<std::string, std::string>> parse_options(const std::string& s)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::stringstream in(s);
    std::string item;
    while(std::getline(in, item, ','))
    {
        size_t eq = item.find('=');
        if(eq==std::string::npos || eq==0)
        die("bad option " + item);
        out.push_back({item.substr(0, eq), item.substr(eq+1)});
    }
    return out;
}

// The engine's binary: `ref` itself if it is one, else that git ref built.
static std::string engine_binary(const std::string& ref, int tt, Checkout& c, const std::string& root)
{
    struct stat st;
    if(stat(ref.c_str(), &st)==0 && S_ISREG(st.st_mode) && access(ref.c_str(), X_OK)==0)
    return ref;
    c.ref = ref;
    std::string error = checkout(c, root, "/tmp/ascaniusfish_tempo");
    if(!error.empty())
    die(error);
    const std::string binary = "/tmp/ascaniusfish_tempo_uci_" + std::to_string(getpid());
    std::printf("building %s with tt=%d ...\n", c.label.c_str(), tt);
    std::fflush(stdout);
    const std::string log = run_capture("cd '" + c.dir + "' && g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas"
        " -Wno-parentheses -Wno-unused-variable -DNDEBUG -pthread -DTT_EXPONENT=" + std::to_string(tt)
        + " -o '" + binary + "' ascaniusfish_uci.cpp 2>&1 | grep -E 'error' | head -5");
    if(access(binary.c_str(), X_OK)!=0)
    die("build failed for " + c.label + ":\n" + log);
    return binary;
}

// s(1..max_depth) of one position; false when the engine died.
static bool search(Engine& e, Sample& s, int max_depth)
{
    s.score.assign(max_depth+1, NO_SCORE);
    e.send("ucinewgame");
    e.send("position fen " + s.fen);
    if(!e.ready())
    return false;
    e.send("go depth " + std::to_string(max_depth));
    const long long deadline = now_ms() + 600000;
    std::string line;
    while(e.read_line(line, deadline))
    {
        if(line.compare(0, 9, "bestmove ")==0)
        return true;
        Search_Info info;
        if(parse_info(line, info) && info.depth>=1 && info.depth<=max_depth)
        s.score[info.depth] = info.score_kind=="cp" ? std::atoi(info.score_value.c_str()) : NO_SCORE;
    }
    return false;
}

struct Median
{
    double median = NAN, lo = NAN, hi = NAN;
    int n = 0;
};

// The median and its 95% CI from order statistics (normal approximation of
// the binomial: ranks n/2 -+ 0.98*sqrt(n)).
static Median median_of(std::vector<double> v)
{
    Median m;
    m.n = (int)v.size();
    if(v.empty())
    return m;
    std::sort(v.begin(), v.end());
    const int n = m.n;
    m.median = n%2 ? v[n/2] : (v[n/2-1]+v[n/2])/2;
    const double half = 0.98*std::sqrt((double)n);
    m.lo = v[std::max(0, (int)std::floor(n/2.0 - half))];
    m.hi = v[std::min(n-1, (int)std::ceil(n/2.0 + half))];
    return m;
}

int main(int argc, char** argv)
{
    std::map<std::string, std::string> opt;
    for(int i=1;i<argc;i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq==std::string::npos || eq==0)
        die("expected key=value, got " + a);
        if(eq+1<a.size())
        opt[a.substr(0, eq)] = a.substr(eq+1);
    }
    for(auto& [k, v] : opt)
    if(std::string(" engine positions per_bin depths agree jobs minutes tt seed options out ").find(" " + k + " ")==std::string::npos)
    die("unknown option " + k);
    auto get = [&](const std::string& k, const std::string& def) { return opt.count(k) ? opt[k] : def; };

    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    signal(SIGPIPE, SIG_IGN);

    std::vector<int> depths;
    {
        std::stringstream in(get("depths", "2,4,6"));
        std::string d;
        while(std::getline(in, d, ','))
        {
            depths.push_back(std::atoi(d.c_str()));
            if(depths.back()<1 || depths.back()%2) die("depths must be even and >= 2: " + d);
        }
        if(depths.empty()) die("no depths");
    }
    const int max_depth = *std::max_element(depths.begin(), depths.end()) + 2;
    const int per_bin = std::atoi(get("per_bin", "600").c_str());
    const int agree = std::atoi(get("agree", "30").c_str());
    const int jobs = std::max(1, std::atoi(get("jobs", "4").c_str()));
    const double minutes = std::atof(get("minutes", "10").c_str());
    const int tt = std::atoi(get("tt", "17").c_str());

    // Positions: shuffled, quiet roots, up to per_bin per phase bin.
    std::vector<Sample> samples;
    {
        const std::string file = get("positions", "nets/nne_d6_test.tsv");
        std::ifstream in(file);
        if(!in) die("cannot read " + file);
        std::vector<std::string> fens;
        std::string line;
        while(std::getline(in, line))
        {
            if(line.empty() || line[0]=='#') continue;
            std::vector<std::string> col;
            std::stringstream ls(line);
            std::string c;
            while(std::getline(ls, c, '\t')) col.push_back(c);
            fens.push_back(col.size()>1 ? col[1] : col[0]);
        }
        std::mt19937_64 rng(std::atoll(get("seed", "70").c_str()));
        std::shuffle(fens.begin(), fens.end(), rng);
        int in_bin[BINS] = {0};
        int not_quiet = 0;
        for(const std::string& fen : fens)
        {
            BB pos;
            if(!uci_parse_fen(fen, pos)) continue;
            Sample s;
            s.fen = fen;
            s.phase = phase_of(pos);
            int& n = in_bin[bin_of(s.phase)];
            if(n>=per_bin) continue;
            if(!quiet_root(pos)) { not_quiet++; continue; }
            n++;
            samples.push_back(s);
        }
        std::printf("%zu positions from %s (%d not quiet skipped), per bin:", samples.size(), file.c_str(), not_quiet);
        for(int b=0;b<BINS;b++) std::printf(" %d", in_bin[b]);
        std::printf("\n");
    }

    Checkout checkout_dir;
    const std::string root = repo_root();
    const std::string binary = engine_binary(get("engine", "."), tt, checkout_dir, root);
    std::vector<std::pair<std::string, std::string>> options = {{"UseNNE", "false"}};
    for(const auto& o : parse_options(get("options", "")))
    options.push_back(o);

    // Bins interleaved, so a run cut short by `minutes` still covers every bin.
    {
        std::vector<std::vector<Sample>> by_bin(BINS);
        for(Sample& s : samples) by_bin[bin_of(s.phase)].push_back(std::move(s));
        samples.clear();
        for(size_t k=0;;k++)
        {
            bool any = false;
            for(int b=0;b<BINS;b++)
            if(k<by_bin[b].size()) { samples.push_back(std::move(by_bin[b][k])); any = true; }
            if(!any) break;
        }
    }

    std::atomic<size_t> next{0};
    std::atomic<int> done{0};
    std::vector<char> searched(samples.size(), 0);
    const long long stop_at = now_ms() + (long long)(minutes*60000);
    const long long started = now_ms();
    std::mutex print_mutex;
    std::vector<std::thread> workers;
    for(int j=0;j<jobs;j++)
    workers.emplace_back([&]
    {
        Engine e;
        e.path = binary;
        e.options = options;
        if(!e.start())
        {
            std::lock_guard<std::mutex> lock(print_mutex);
            std::fprintf(stderr, "engine %s did not start\n", binary.c_str());
            return;
        }
        for(size_t i; (i = next++)<samples.size() && now_ms()<stop_at;)
        {
            if(!search(e, samples[i], max_depth))
            {
                std::lock_guard<std::mutex> lock(print_mutex);
                std::fprintf(stderr, "engine failed on %s, restarting\n", samples[i].fen.c_str());
                if(!e.restart()) return;
                continue;
            }
            searched[i] = 1;
            const int k = ++done;
            if(k%200==0)
            {
                std::lock_guard<std::mutex> lock(print_mutex);
                std::fprintf(stderr, "  %d/%zu positions, %.0f s\n", k, samples.size(), (now_ms()-started)/1000.0);
            }
        }
        e.stop();
    });
    for(std::thread& w : workers) w.join();
    remove_checkout(checkout_dir, root);
    if(binary!=get("engine", ".")) std::remove(binary.c_str());  // only a binary built here

    if(!get("out", "").empty())
    {
        std::ofstream out(get("out", ""));
        out << "#fen\tphase";
        for(int d=1;d<=max_depth;d++) out << "\ts" << d;
        out << "\n";
        for(size_t i=0;i<samples.size();i++)
        if(searched[i])
        {
            out << samples[i].fen << "\t" << samples[i].phase;
            for(int d=1;d<=max_depth;d++)
            {
                const int v = samples[i].score[d];
                out << "\t";
                if(v==NO_SCORE) out << "-"; else out << v;
            }
            out << "\n";
        }
    }

    // T̂ per bin and depth.
    std::vector<std::vector<std::vector<double>>> t(BINS, std::vector<std::vector<double>>(depths.size()));
    std::vector<std::vector<double>> all(depths.size());
    for(size_t i=0;i<samples.size();i++)
    if(searched[i])
    for(size_t k=0;k<depths.size();k++)
    {
        const std::vector<int>& s = samples[i].score;
        const int d = depths[k];
        if(s[d]==NO_SCORE || s[d+1]==NO_SCORE || s[d+2]==NO_SCORE || std::abs(s[d]-s[d+2])>agree)
        continue;
        const double v = (s[d+1] - (s[d]+s[d+2])/2.0)/2.0;
        t[bin_of(samples[i].phase)][k].push_back(v);
        all[k].push_back(v);
    }

    std::printf("\n%d positions searched to depth %d in %.0f s, %d engines, UseNNE=false, agree=%d cp\n",
                done.load(), max_depth, (now_ms()-started)/1000.0, jobs, agree);
    std::printf("median tempo bias T̂ in cp [95%% CI] (n); positive = the side that just moved gets too much\n\n");
    std::printf("%-14s", "phase (0..78)");
    for(int d : depths) std::printf("  %-24s", ("d=" + std::to_string(d)).c_str());
    std::printf("\n");
    auto cell = [](const Median& m)
    {
        char buf[64];
        if(m.n==0) std::snprintf(buf, sizeof buf, "-");
        else std::snprintf(buf, sizeof buf, "%+6.1f [%+.1f,%+.1f] (%d)", m.median, m.lo, m.hi, m.n);
        return std::string(buf);
    };
    std::vector<std::vector<Median>> med(BINS, std::vector<Median>(depths.size()));
    for(int b=0;b<BINS;b++)
    {
        const int lo = (b*(PHASE_MAX+1)+BINS-1)/BINS, hi = b==BINS-1 ? 99 : ((b+1)*(PHASE_MAX+1)+BINS-1)/BINS-1;
        char label[32];
        std::snprintf(label, sizeof label, "%2d..%-2d", lo, hi==99 ? PHASE_MAX : hi);
        std::printf("%-14s", label);
        for(size_t k=0;k<depths.size();k++)
        {
            med[b][k] = median_of(t[b][k]);
            std::printf("  %-24s", cell(med[b][k]).c_str());
        }
        std::printf("\n");
    }
    std::printf("%-14s", "all");
    for(size_t k=0;k<depths.size();k++) std::printf("  %-24s", cell(median_of(all[k])).c_str());
    std::printf("\n\n");

    // T(p) = T_e + (T_o - T_e)*p/78, least squares over the bin medians at the
    // bins' mean phase, weighted by n.
    for(size_t k=0;k<depths.size();k++)
    {
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for(int b=0;b<BINS;b++)
        {
            if(med[b][k].n==0) continue;
            double mean_p = 0;
            int m = 0;
            for(size_t i=0;i<samples.size();i++)
            if(searched[i] && bin_of(samples[i].phase)==b) { mean_p += samples[i].phase; m++; }
            const double x = mean_p/m/PHASE_MAX, y = med[b][k].median, w = med[b][k].n;
            sw += w; sx += w*x; sy += w*y; sxx += w*x*x; sxy += w*x*y;
        }
        const double den = sw*sxx - sx*sx;
        if(sw==0 || std::fabs(den)<1e-12) continue;
        const double slope = (sw*sxy - sx*sy)/den, t_e = (sy - slope*sx)/sw;
        std::printf("fit d=%d: T_o = %+.1f (full material), T_e = %+.1f (bare kings)\n", depths[k], t_e+slope, t_e);
    }
    return 0;
}
