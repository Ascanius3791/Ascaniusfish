// OWNERSHIP=Claude
// make bench: fixed-depth search (iterative deepening 1..depth, like UCI
// "go depth N") over a fixed set of positions, TT cleared before each one.
// The total node count is a signature of search behaviour: it changes only
// when the search itself changes, never with machine speed.
//
//   ./tools/bench [depth] [-q] [nne=<file>] [narrow=0|1|2]
// -q prints only the final "bench:" line (used by tools/speed_compare).
// nne=<file> evaluates quiet leaves with that eval-correction net (UCI UseNNE,
// #52); the node count is then a different signature.
// narrow=n sets tt_narrowing (#65); only a -DTT_BOUNDS_NEVER_NARROW=0 build reads it.
// The last line is always: bench: nodes <N> time_ms <T> nps <X>
#include "../lib/uci.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr int BENCH_DEFAULT_DEPTH = 3;  // ~12 s; the search is slow per node (full eval of every child for move ordering)

// Openings, middlegames with tactics, and endgames (incl. the perft positions).
static const char* const BENCH_FENS[] =
{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
    "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14",
    "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
    "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
    "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
    "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
    "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
    "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
    "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
    "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
    "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
    "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26",
    "6k1/3b3r/1p1p4/p1n2p2/1PPNpP1q/P3Q1p1/1R1RB1P1/5K2 b - - 0 1",
    "r2r1n2/pp2bk2/2p1p2p/3q4/3PN1QP/2P3R1/P4PP1/5RK1 w - - 0 1",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 0 1",
    "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",
    "8/8/8/5N2/8/p7/8/2NK3k w - - 0 1",
    "8/3k4/8/8/8/4B3/4KB2/2B5 w - - 0 1",
    "8/8/1P6/5pr1/8/4R3/7k/2K5 w - - 0 1",
    "8/2p4P/8/kr6/6R1/8/8/1K6 w - - 0 1",
    "8/8/3P3k/8/1p6/8/1P6/1K3n2 b - - 0 1",
    "8/R7/2q5/8/6k1/8/1P5p/K6R w - - 0 124",
    "8/8/8/8/8/6k1/6p1/6K1 w - - 0 1",
};

int main(int argc, char** argv)
{
    int depth = BENCH_DEFAULT_DEPTH;
    bool quiet = false;
    const char* nne_file = nullptr;
    for(int i=1;i<argc;i++)
    {
        if(std::strcmp(argv[i], "-q")==0) quiet = true;
        else if(std::strncmp(argv[i], "nne=", 4)==0) nne_file = argv[i]+4;
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
    for(const char* fen : BENCH_FENS)
    {
        index++;
        BB root;
        if(!uci_parse_fen(fen, root))
        {
            std::fprintf(stderr, "invalid bench FEN: %s\n", fen);
            return 1;
        }
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
