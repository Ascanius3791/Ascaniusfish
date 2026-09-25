// OWNERSHIP=Claude
// make perft: checks move generation against the known perft node counts of
// the standard test positions (chessprogramming.org/Perft_Results) and
// prints time and Mnps. Exit code 1 if any count is wrong.
//
//   ./tools/perft            every position to its listed max depth (>= 5)
//   ./tools/perft 4          cap every position at depth 4 (quick check)
#include "../lib/uci.hpp"

#include <cstdio>
#include <cstdlib>

struct Perft_Case
{
    const char* name;
    const char* fen;
    long long nodes[8];  // nodes[d] = perft(d); 0 = not checked
};

static const Perft_Case CASES[] =
{
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        {0, 20, 400, 8902, 197281, 4865609, 119060324}},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        {0, 48, 2039, 97862, 4085603, 193690690}},
    {"pos3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        {0, 14, 191, 2812, 43238, 674624, 11030083}},
    {"pos4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        {0, 6, 264, 9467, 422333, 15833292}},
    {"pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        {0, 44, 1486, 62379, 2103487, 89941194}},
    {"pos6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        {0, 46, 2079, 89890, 3894594, 164075551}},
};

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    int depth_cap = argc>1 ? std::atoi(argv[1]) : 99;
    BB* buf = new BB[1<<16];
    bool all_ok = true;
    long long total_nodes = 0;
    double total_s = 0;

    std::printf("%-9s %5s %12s %12s %8s %7s\n", "position", "depth", "nodes", "expected", "time_s", "Mnps");
    for(const Perft_Case& c : CASES)
    {
        BB root;
        if(!uci_parse_fen(c.fen, root))
        {
            std::printf("%-9s invalid FEN\n", c.name);
            all_ok = false;
            continue;
        }
        int depth = 0;
        for(int d=1; d<8 && c.nodes[d]; d++)
        depth = d;
        depth = std::min(depth, depth_cap);

        auto start = std::chrono::steady_clock::now();
        long long nodes = perft(&root, depth, buf);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        bool ok = nodes==c.nodes[depth];
        all_ok &= ok;
        total_nodes += nodes;
        total_s += s;
        std::printf("%-9s %5d %12lld %12lld %8.2f %7.1f  %s\n", c.name, depth, nodes, c.nodes[depth], s, nodes/s/1e6, ok ? "ok" : "MISMATCH");
    }
    std::printf("total: %lld nodes in %.2f s, %.1f Mnps -> %s\n", total_nodes, total_s, total_nodes/total_s/1e6, all_ok ? "all correct" : "FAILED");
    delete[] buf;
    return all_ok ? 0 : 1;
}
