// OWNERSHIP=Claude
// Delta pruning margin study (#75). Build with -DDELTA_STATS, from the repo root:
//   g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable \
//       -DNDEBUG -DDELTA_STATS -o diagnostics/delta_margin diagnostics/delta_margin.cpp
//   ./diagnostics/delta_margin [depth=6] [nne=nets/nne_d6.bin]
// Searches the bench positions and tools/openings*.epd to a fixed depth with
// delta pruning off, recording every quiescence capture the rule could prune
// (stand pat + SEE short of the window; the victim's value is never below
// SEE). Then each recorded capture is searched with the window it had, and for
// each margin M the table says how many captures a rule would prune and how
// many of those would have raised alpha (lowered beta for black): a wrong prune.
// Two rules: stand pat + victim value + M, and stand pat + SEE + M.
#include "../lib/uci.hpp"
#include "../tools/bench_positions.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

struct Delta_Sample { BB pos; Move m; int stand_pat, alpha, beta, value, see; };

static std::vector<Delta_Sample> samples;
static long long recorded = 0;
static std::mt19937_64 rng(75);
constexpr size_t MAX_SAMPLES = 200000;

void delta_record(const BB* pos, const Move& m, int stand_pat, int alpha, int beta, int value, int see)
{
    Delta_Sample s{*pos, m, stand_pat, alpha, beta, value, see};
    recorded++;
    if(samples.size() < MAX_SAMPLES) samples.push_back(s);
    else { size_t j = rng() % recorded; if(j < MAX_SAMPLES) samples[j] = s; }  // reservoir
}

int main(int argc, char** argv)
{
    int depth = 6;
    const char* nne_file = nullptr;
    for(int i=1;i<argc;i++)
    {
        if(std::strncmp(argv[i], "depth=", 6)==0) depth = std::atoi(argv[i]+6);
        else if(std::strncmp(argv[i], "nne=", 4)==0) nne_file = argv[i]+4;
    }
    if(nne_file)
    {
        std::string error;
        if(!nne::load(nne_file, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        nne::enabled = true;
    }
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::vector<std::string> fens(std::begin(BENCH_FENS), std::end(BENCH_FENS));
    for(const char* f : {"tools/openings.epd", "tools/openings_large.epd"})
    {
        std::vector<std::string> more = load_epd_fens(f);
        fens.insert(fens.end(), more.begin(), more.end());
    }

    lookup_table* table = new lookup_table;
    BB* wfh = new BB[UCI_WFH_SIZE];
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable* cycle_table = new CuckooCycleTable;
    long long nodes_before = search_nodes;
    delta_recording = true;
    for(const std::string& fen : fens)
    {
        BB root;
        if(!uci_parse_fen(fen.c_str(), root)) { std::fprintf(stderr, "invalid FEN: %s\n", fen.c_str()); return 1; }
        table->reset();
        path_history[0] = root;
        for(int d=1; d<=depth; d++)
        minimax(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, 0, cycle_table);
    }
    delta_recording = false;
    std::printf("%zu positions, depth %d, net %s: %lld nodes, %lld candidate captures, %zu searched\n",
                fens.size(), depth, nne_file ? nne_file : "off", search_nodes-nodes_before, recorded, samples.size());

    // shortfall: how far below alpha (white's view) stand pat + estimate stays;
    // raised: the searched capture beat alpha anyway.
    struct Row { int value_short, see_short; bool raised; int excess; };
    std::vector<Row> rows;
    rows.reserve(samples.size());
    for(const Delta_Sample& s : samples)
    {
        BB child;
        make_move(&s.pos, s.m, &child);
        const int r = minimax_tactical(&child, wfh, WEIGHTS_OG, s.alpha, s.beta, nullptr).eval;
        const bool w = s.pos.white_move;
        const long long sp = s.stand_pat;
        Row row;
        row.value_short = (int)(w ? s.alpha - (sp + s.value) : (sp - s.value) - s.beta);
        row.see_short   = (int)(w ? s.alpha - (sp + s.see)   : (sp - s.see)   - s.beta);
        row.raised = w ? r > s.alpha : r < s.beta;
        row.excess = (int)std::clamp<long long>(w ? (long long)r - (sp + s.value) : (sp - s.value) - (long long)r, -100000, 100000);
        rows.push_back(row);
    }

    std::printf("\nmargin | victim value: pruned  wrong  wrong%%  | SEE: pruned  wrong  wrong%%\n");
    for(int M : {0, 50, 100, 150, 200, 250, 300, 400, 500, 600, 800, 1000})
    {
        long long pv=0, wv=0, ps=0, ws=0;
        for(const Row& r : rows)
        {
            if(r.value_short >= M) { pv++; wv += r.raised; }
            if(r.see_short >= M)   { ps++; ws += r.raised; }
        }
        std::printf("%6d | %13lld %6lld %7.3f | %11lld %6lld %7.3f\n", M, pv, wv, pv ? 100.0*wv/pv : 0.0, ps, ws, ps ? 100.0*ws/ps : 0.0);
    }

    // How much more than the victim the searched capture gained, over the value-rule candidates.
    std::vector<int> ex;
    for(const Row& r : rows) if(r.value_short >= 0) ex.push_back(r.excess);
    std::sort(ex.begin(), ex.end());
    std::printf("\nexcess = searched result - (stand pat + victim), %zu value-rule candidates:\n", ex.size());
    for(double q : {0.5, 0.9, 0.99, 0.999, 1.0})
    if(!ex.empty()) std::printf("  p%-5g %6d\n", q*100, ex[std::min(ex.size()-1, (size_t)(q*(ex.size()-1)))]);
    return 0;
}
