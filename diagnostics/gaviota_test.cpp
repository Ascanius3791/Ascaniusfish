// OWNERSHIP=Claude
//
// Checks the Gaviota DTM probe (lib/gaviota.hpp, issue #77):
//   1. the reference (tools/gaviota_reference.txt, Lichess's DTM for the
//      positions of tools/syzygy_reference.txt): result and plies must match;
//   2. every reference position against its own children, each probed wherever
//      it lands (a capture into a smaller table, a promotion into another, en
//      passant): a win is 1 + the quickest child the opponent loses, a loss
//      1 + the slowest child the opponent wins, a draw has no child the
//      opponent loses. Mate / stalemate when there is no move;
//   3. RSS after init() and after probing every table, and the probe speed.
// Exit code 1 on any mismatch.
//
//   ./diagnostics/gaviota_test [tables_dir=~/gaviota] [reference=tools/gaviota_reference.txt]
//   make gaviota-test [GAVIOTA_PATH=~/gaviota]
//
// Needs the prober, so it is built by the Makefile (-DWITH_GAVIOTA and libgtb.a).

#include "../lib/uci.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

static long rss_kb()
{
    std::ifstream f("/proc/self/status");
    std::string line;
    while(std::getline(f, line))
    if(line.rfind("VmRSS:", 0) == 0) return std::atol(line.c_str() + 6);
    return -1;
}

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    const std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    const std::string dir = argc>1 ? argv[1] : home + "/gaviota";
    const std::string ref_path = argc>2 ? argv[2] : "tools/gaviota_reference.txt";
    if(!gaviota::compiled_in())
    {
        std::printf("FAIL: built without -DWITH_GAVIOTA\n");
        return 1;
    }
    const long rss0 = rss_kb();
    const int pieces = gaviota::init(dir, 32);
    std::printf("init: up to %d pieces from %s, RSS %ld -> %ld kB\n", pieces, dir.c_str(), rss0, rss_kb());
    if(pieces < 5)
    {
        std::printf("FAIL: need the 3-4-5 piece tables\n");
        return 1;
    }

    std::ifstream in(ref_path);
    int checked = 0, no_dtm = 0, ref_bad = 0, child_bad = 0, missing = 0;
    long long probes = 0;
    BB buf[MAX_ORDERED_MOVES];
    const auto t0 = std::chrono::steady_clock::now();
    for(std::string line; std::getline(in, line); )
    {
        if(line.empty() || line[0]=='#') continue;
        const size_t semi = line.find(';');
        const std::string fen = line.substr(0, semi), want = line.substr(semi+1);
        BB pos;
        if(!uci_parse_fen(fen, pos)) { std::printf("bad FEN %s\n", fen.c_str()); ref_bad++; continue; }
        int res, plies;
        probes++;
        if(!gaviota::probe_dtm(&pos, res, plies)) { std::printf("not found: %s\n", fen.c_str()); missing++; continue; }
        checked++;

        // 1. the reference
        if(want=="-")
        no_dtm++;
        else
        {
            const bool ok = want=="mated" ? res==-1 && plies==0
                          : res==0 ? want=="0"
                          : std::atoi(want.c_str()) == res*plies;
            if(!ok)
            {
                std::printf("reference: %s  ours %+d plies %d, Lichess %s\n", fen.c_str(), res, plies, want.c_str());
                ref_bad++;
            }
        }

        // 2. the children
        const int n = std::get<0>(all_moves(&pos, buf, MAX_ORDERED_MOVES));
        int win = -1, lose = -1;  // quickest child the opponent loses, slowest they win
        bool known = true;
        for(int i=0;i<n && known;i++)
        {
            int r, p;
            known = gaviota::probe_dtm(&buf[i], r, p);
            probes++;
            if(r==-1 && (win<0 || p<win)) win = p;
            if(r==1 && (lose<0 || p>lose)) lose = p;
        }
        bool ok = known;
        if(!known)
        std::printf("children: %s  a child is not found\n", fen.c_str());
        else if(n==0)
        ok = res==0 ? plies==0 && !in_check(pos.Board, pos.white_move) : res==-1 && plies==0;
        else if(res==1)
        ok = win>=0 && plies==win+1;
        else if(res==-1)
        ok = win<0 && lose>=0 && plies==lose+1;
        else
        ok = win<0;
        if(!ok)
        {
            if(known)
            std::printf("children: %s  ours %+d plies %d, children: quickest loss %d, slowest win %d (n=%d)\n",
                        fen.c_str(), res, plies, win, lose, n);
            child_bad++;
        }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    std::printf("%d positions: %d reference mismatches (%d without a Lichess DTM), %d child mismatches, %d not found\n",
                checked, ref_bad, no_dtm, child_bad, missing);
    std::printf("%lld probes in %.2f s (%.1f us each, cold disk cache included), RSS %ld kB\n",
                probes, s, s*1e6/std::max(1LL, probes), rss_kb());
    const bool pass = checked>0 && ref_bad==0 && child_bad==0 && missing==0;
    std::printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
