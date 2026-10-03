// OWNERSHIP=Claude
// make bench: fixed-depth search (iterative deepening 1..depth, like UCI
// "go depth N") over a fixed set of positions, TT cleared before each one.
// The total node count is a signature of search behaviour: it changes only
// when the search itself changes, never with machine speed.
//
//   ./tools/bench [depth] [-q] [nne=<file>] [narrow=0|1|2] [epd=<file>]
// -q prints only the final "bench:" line (used by tools/speed_compare).
// epd=<file> searches that suite's positions after the bench's own, e.g.
// epd=tools/openings.epd for the 130 positions of #80/#81.
// keep: the TT is not cleared between positions; each one is a new search
// (new_search(), as UCI "go" does), so the table runs full like in a game (#81).
// nne=<file> evaluates quiet leaves with that eval-correction net (UCI UseNNE,
// #52); the node count is then a different signature.
// narrow=n sets tt_narrowing (#65); only a -DTT_BOUNDS_NEVER_NARROW=0 build reads it.
// The last line is always: bench: nodes <N> time_ms <T> nps <X>
#include "../lib/uci.hpp"
#include "bench_positions.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr int BENCH_DEFAULT_DEPTH = 3;  // ~12 s; the search is slow per node (full eval of every child for move ordering)

int main(int argc, char** argv)
{
    int depth = BENCH_DEFAULT_DEPTH;
    bool quiet = false;
    bool keep = false;
    const char* nne_file = nullptr;
    std::vector<std::string> fens(std::begin(BENCH_FENS), std::end(BENCH_FENS));
    for(int i=1;i<argc;i++)
    {
        if(std::strcmp(argv[i], "-q")==0) quiet = true;
        else if(std::strcmp(argv[i], "keep")==0) keep = true;
        else if(std::strncmp(argv[i], "nne=", 4)==0) nne_file = argv[i]+4;
        else if(std::strncmp(argv[i], "epd=", 4)==0)
        {
            std::vector<std::string> more = load_epd_fens(argv[i]+4);
            fens.insert(fens.end(), more.begin(), more.end());
        }
        else if(std::strncmp(argv[i], "narrow=", 7)==0) tt_narrowing = std::atoi(argv[i]+7);//#65, only a -DTT_BOUNDS_NEVER_NARROW=0 build reads it
        else depth = std::atoi(argv[i]);
    }
    if(nne_file)
    {
        std::string error;
        if(!nne::load(nne_file, error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        nne::enabled = true;
    }

    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    lookup_table* table = new lookup_table;
    BB* wfh = new BB[UCI_WFH_SIZE];
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable* cycle_table = new CuckooCycleTable;

    long long total_nodes = 0;
    long long total_ns = 0;
    int index = 0;
    for(const std::string& fen_string : fens)
    {
        const char* fen = fen_string.c_str();
        index++;
        BB root;
        if(!uci_parse_fen(fen, root))
        {
            std::fprintf(stderr, "invalid bench FEN: %s\n", fen);
            return 1;
        }
        if(keep)
        table->new_search();
        else
        table->reset();
        path_history[0] = root;

        long long nodes_before = search_nodes;
        long long start_ns = steady_now_ns();
        PV_Line pv;
        for(int d=1; d<=depth; d++)
        pv = minimax(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, 0, cycle_table);
        long long ns = steady_now_ns()-start_ns;
        long long nodes = search_nodes-nodes_before;
        total_nodes += nodes;
        total_ns += ns;
        if(!quiet)
        std::printf("%2d %-72s score %-10s nodes %10lld  %6lld ms\n", index, fen, uci_score(pv.eval, root.white_move).c_str(), nodes, ns/1000000);
    }
    long long ms = std::max(1LL, total_ns/1000000);
    std::printf("bench: nodes %lld time_ms %lld nps %lld\n", total_nodes, ms, (long long)(total_nodes*1e9/std::max(1LL, total_ns)));

    delete table;
    delete[] wfh;
    delete[] path_history;
    delete cycle_table;
    return 0;
}
