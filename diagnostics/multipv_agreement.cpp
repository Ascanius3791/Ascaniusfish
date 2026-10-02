// OWNERSHIP=Claude
// MultiPV (#69): how often line 1 at K>1 agrees with the plain search (K=1)
// on the bench positions. Each position and K is searched like UCI
// "go depth D" (iterative deepening, TT and killers cleared first), through
// multipv_search(), the function the UCI loop calls.
//
//   diagnostics/multipv_agreement [depth=6] [k=2,3,5,256]
//
// Line 1 at K>1 can differ only through the TT entries the passes 2..K
// leave below the root. Prints a row per position and K, then per K: the
// positions whose first move agrees, the mean |score difference| and the
// node and time ratios against K=1.
#include "../lib/uci.hpp"
#include "../tools/bench_positions.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

struct Result
{
    std::string move;
    int eval = 0;
    long long nodes = 0, ns = 0;
};

static Result run(const BB& root, int depth, int k, lookup_table* table, BB* wfh, BB* path_history, CuckooCycleTable* cycle_table)
{
    table->reset();
    clear_killer_moves();
    path_history[0] = root;
    Result r;
    long long nodes_before = search_nodes;
    long long start_ns = steady_now_ns();
    std::vector<PV_Line> lines;
    for(int d=1; d<=depth; d++)
    lines = multipv_search(root, wfh, d, table, path_history, 0, cycle_table, k);
    r.ns = steady_now_ns()-start_ns;
    r.nodes = search_nodes-nodes_before;
    r.eval = lines[0].eval;
    BB child;
    if(lines[0].current_lenght>0)
    {
        make_move(&root, lines[0].at(0), &child);
        r.move = get_UCI(&root, &child);
    }
    return r;
}

// A score in centipawns for the difference; mates count as 100 pawns.
static int cp(int eval)
{
    if(eval >= INT_MAX-max_mating_seq) return 10000;
    if(eval <= INT_MIN+max_mating_seq) return -10000;
    return std::max(-10000, std::min(10000, eval));
}

int main(int argc, char** argv)
{
    int depth = 6;
    std::vector<int> ks = {2, 3, 5, 256};
    for(int i=1;i<argc;i++)
    {
        if(std::strncmp(argv[i], "depth=", 6)==0) depth = std::atoi(argv[i]+6);
        else if(std::strncmp(argv[i], "k=", 2)==0)
        {
            ks.clear();
            std::stringstream in(argv[i]+2);
            std::string t;
            while(std::getline(in, t, ','))
            ks.push_back(std::atoi(t.c_str()));
        }
    }

    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    lookup_table* table = new lookup_table;
    BB* wfh = new BB[UCI_WFH_SIZE];
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable* cycle_table = new CuckooCycleTable;

    const int nk = (int)ks.size();
    std::vector<int> agree(nk, 0), positions(nk, 0);
    std::vector<long long> abs_diff(nk, 0), nodes(nk, 0), base_nodes(nk, 0), ns(nk, 0), base_ns(nk, 0);
    int index = 0;
    for(const char* fen : BENCH_FENS)
    {
        index++;
        BB root;
        uci_parse_fen(fen, root);
        BB children[256];
        const int n = std::get<0>(all_moves(&root, children));
        if(n==0)
        continue;
        const Result base = run(root, depth, 1, table, wfh, path_history, cycle_table);
        std::printf("%2d K=1   %-6s %-12s %9lld nodes\n", index, base.move.c_str(), uci_score(base.eval, root.white_move).c_str(), base.nodes);
        for(int j=0;j<nk;j++)
        {
            const int k = std::min(ks[j], n);
            const Result r = run(root, depth, k, table, wfh, path_history, cycle_table);
            const bool same = r.move==base.move;
            positions[j]++;
            agree[j] += same;
            abs_diff[j] += std::abs(cp(r.eval)-cp(base.eval));
            nodes[j] += r.nodes;
            base_nodes[j] += base.nodes;
            ns[j] += r.ns;
            base_ns[j] += base.ns;
            std::printf("   K=%-3d %-6s %-12s %9lld nodes %s\n", k, r.move.c_str(), uci_score(r.eval, root.white_move).c_str(), r.nodes, same ? "" : " <- differs");
        }
    }
    std::printf("\ndepth %d, %d positions; K above the legal moves counts as all of them\n", depth, positions.empty() ? 0 : positions[0]);
    for(int j=0;j<nk;j++)
    std::printf("K=%-3d line 1 agrees %2d/%d, mean |score diff| %.1f cp, nodes x%.2f, time x%.2f\n",
                ks[j], agree[j], positions[j], (double)abs_diff[j]/std::max(1, positions[j]),
                (double)nodes[j]/std::max(1LL, base_nodes[j]), (double)ns[j]/std::max(1LL, base_ns[j]));

    delete table;
    delete[] wfh;
    delete[] path_history;
    delete cycle_table;
    return 0;
}
