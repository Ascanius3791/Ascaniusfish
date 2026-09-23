// OWNERSHIP=Claude
//
// Verifies and benchmarks all_moves() (src/move_generation.cpp, TODO.md item 1 -
// now the live implementation used throughout the engine) against all_moves_old()
// (the previous implementation, kept as a reference/fallback in the same file).
//
// Two kinds of check:
//   1. Full move-list agreement, recursively, from six standard perft test
//      positions (startpos + the five CPW "perft results" positions that are
//      specifically designed to exercise pins, checks, en passant, castling and
//      promotions). Any single-move disagreement anywhere in the tree is reported
//      with the exact board it happened on.
//   2. Perft node counts against known-correct values for startpos and Kiwipete
//      (the two positions the exact numbers below are highest-confidence for),
//      plus an old-vs-new node count cross-check for all six positions to a depth
//      where full move-list diffing would be too slow.
// Then a speed benchmark: perft(startpos, depth) timed with both generators.
//
// Build (from repo root, like any other diagnostic - see CLAUDE.md):
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG \
//       -o diagnostics/legal_movegen_perft_compare diagnostics/legal_movegen_perft_compare.cpp

#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

using Key = std::tuple<int,int,int>; // from, to, promotion_piece_type

static std::vector<Key> move_keys(const std::vector<Move>& mv)
{
    std::vector<Key> keys;
    keys.reserve(mv.size());
    for(const Move& m : mv) keys.push_back({m.from, m.to, m.promotion_piece_type});
    std::sort(keys.begin(), keys.end());
    return keys;
}

static void print_keys(const std::vector<Key>& keys)
{
    for(const Key& k : keys)
        std::cout << "(" << std::get<0>(k) << "->" << std::get<1>(k) << ",p" << std::get<2>(k) << ") ";
    std::cout << "\n";
}

static std::string sq_name(int sq)
{
    if(sq < 0 || sq > 63) return "??(" + std::to_string(sq) + ")";
    std::string s;
    s += char('a' + sq % 8);
    s += char('1' + sq / 8);
    return s;
}

static std::string piece_at(const BB& pos, int sq)
{
    static const char* names[6] = {"P","R","N","B","Q","K"};
    for(int i = 0; i < 12; i++)
        if(pos.Board[i] & (1ULL << sq))
            return std::string(i < 6 ? "w" : "b") + names[i % 6];
    return "-";
}

static void diagnose_diff(const BB& pos, const std::vector<Key>& k_old, const std::vector<Key>& k_new)
{
    std::vector<Key> only_old, only_new;
    std::set_difference(k_old.begin(), k_old.end(), k_new.begin(), k_new.end(), std::back_inserter(only_old));
    std::set_difference(k_new.begin(), k_new.end(), k_old.begin(), k_old.end(), std::back_inserter(only_new));
    for(const Key& k : only_old)
        std::cout << "  only in OLD: " << piece_at(pos, std::get<0>(k)) << " " << sq_name(std::get<0>(k))
                  << "-" << sq_name(std::get<1>(k)) << " (promo=" << std::get<2>(k) << ")\n";
    for(const Key& k : only_new)
        std::cout << "  only in NEW: " << piece_at(pos, std::get<0>(k)) << " " << sq_name(std::get<0>(k))
                  << "-" << sq_name(std::get<1>(k)) << " (promo=" << std::get<2>(k) << ")\n";
}

// Recursively compares all_moves_old() vs all_moves() move lists at every node
// reachable within `depth` plies. Recurses through the OLD generator's resulting
// positions (trusted baseline) so this isolates disagreements in which moves are
// considered legal from any incidental bug in the new generator's own board
// bookkeeping.
static int g_mismatches = 0;
static long g_nodes_checked = 0;

static void compare_recursive(const BB& pos, int depth)
{
    g_nodes_checked++;
    // Each recursive call gets its own buffers (must NOT be shared/static -
    // `pos` for sibling/child calls below aliases into buf_old, so reusing one
    // static buffer across recursion levels would corrupt it mid-traversal).
    std::vector<BB> buf_old(256), buf_new(256);

    auto [n_old, mv_old] = all_moves_old(&pos, buf_old.data(), 255);
    auto [n_new, mv_new] = all_moves(&pos, buf_new.data(), 255);

    auto k_old = move_keys(mv_old);
    auto k_new = move_keys(mv_new);

    if(k_old != k_new)
    {
        g_mismatches++;
        std::cout << "MISMATCH (depth=" << depth << ", node #" << g_nodes_checked << "):\n";
        print(const_cast<uint64_t*>(pos.Board));
        std::cout << "  white_move=" << pos.white_move << " en_passant=" << pos.en_passant
                  << " castle=[" << pos.castle[0][0] << pos.castle[0][1] << "|" << pos.castle[1][0] << pos.castle[1][1] << "]\n";
        std::cout << "  old (" << n_old << "): "; print_keys(k_old);
        std::cout << "  new (" << n_new << "): "; print_keys(k_new);
        diagnose_diff(pos, k_old, k_new);
        if(g_mismatches > 3)
        {
            std::cout << "  ...too many mismatches, stopping this branch's output.\n";
            return;
        }
    }

    if(depth <= 1) return;
    for(int i = 0; i < n_old && g_mismatches <= 3; i++)
        compare_recursive(buf_old[i], depth - 1);
}

// Bulk-counting perft: at depth==1 it counts moves without generating
// grandchildren (standard perft speedup - still an exact leaf count).
using MoveGenFn = std::tuple<int,std::vector<Move>> (*)(const BB* const, BB* const, int);

static uint64_t perft(const BB& pos, int depth, MoveGenFn gen, std::vector<std::vector<BB>>& level_bufs, int level)
{
    if(depth == 0) return 1;
    BB* buffer = level_bufs[level].data();
    int n = std::get<0>(gen(&pos, buffer, (int)level_bufs[level].size() - 1));
    if(depth == 1) return (uint64_t)n;
    uint64_t nodes = 0;
    for(int i = 0; i < n; i++)
        nodes += perft(buffer[i], depth - 1, gen, level_bufs, level + 1);
    return nodes;
}

static uint64_t run_perft(const BB& pos, int depth, MoveGenFn gen)
{
    std::vector<std::vector<BB>> level_bufs(depth + 1, std::vector<BB>(256));
    return perft(pos, depth, gen, level_bufs, 0);
}

struct TestPosition { std::string name; std::string fen; };

int main()
{
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    Zobrist zobrist_init = Zobrist();

    std::vector<TestPosition> positions = {
        {"startpos",  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"kiwipete",  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
        {"position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
        {"position4", "r3k2r/Pppp1ppp/1B3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
        {"position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 0 1"},
        {"position6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 1"},
    };

    std::cout << "=== Part 1: recursive full move-list agreement (all_moves_old vs all_moves) ===\n";
    for(const auto& tp : positions)
    {
        BB pos(tp.fen);
        int depth = (tp.name == "startpos") ? 4 : 3; // deep enough to hit pins/checks/en passant/castling
        g_mismatches = 0;
        g_nodes_checked = 0;
        compare_recursive(pos, depth);
        std::cout << tp.name << ": checked " << g_nodes_checked << " nodes to depth " << depth
                  << ", mismatches=" << g_mismatches << (g_mismatches == 0 ? " OK\n" : " *** FAIL ***\n");
    }

    std::cout << "\n=== Part 2: perft node counts, old vs new vs known-correct ===\n";
    // Known-correct perft counts (see e.g. the chess programming wiki's "Perft
    // Results" page) - only asserted for the two positions given here with high
    // confidence; the other four are still cross-checked old-vs-new below even
    // though their reference numbers aren't hardcoded.
    struct Known { std::string name; int depth; uint64_t nodes; };
    std::vector<Known> known = {
        {"startpos", 1, 20}, {"startpos", 2, 400}, {"startpos", 3, 8902}, {"startpos", 4, 197281},
        {"kiwipete", 1, 48}, {"kiwipete", 2, 2039}, {"kiwipete", 3, 97862},
    };
    for(const auto& k : known)
    {
        auto it = std::find_if(positions.begin(), positions.end(), [&](const TestPosition& p){ return p.name == k.name; });
        BB pos(it->fen);
        uint64_t got_old = run_perft(pos, k.depth, &all_moves_old);
        uint64_t got_new = run_perft(pos, k.depth, &all_moves);
        std::cout << k.name << " perft(" << k.depth << "): expected=" << k.nodes
                  << " old=" << got_old << " new=" << got_new
                  << ((got_old == k.nodes && got_new == k.nodes) ? " OK\n" : " *** FAIL ***\n");
    }

    std::cout << "\nOld-vs-new node count cross-check (all six positions):\n";
    for(const auto& tp : positions)
    {
        BB pos(tp.fen);
        int depth = 3;
        uint64_t got_old = run_perft(pos, depth, &all_moves_old);
        uint64_t got_new = run_perft(pos, depth, &all_moves);
        std::cout << tp.name << " perft(" << depth << "): old=" << got_old << " new=" << got_new
                  << (got_old == got_new ? " OK\n" : " *** FAIL ***\n");
    }

    std::cout << "\n=== Part 3: speed benchmark (perft(startpos, 5), bulk-counting) ===\n";
    {
        BB pos(positions[0].fen);
        int depth = 5; // 4,865,609 nodes

        auto t0 = std::chrono::steady_clock::now();
        uint64_t nodes_old = run_perft(pos, depth, &all_moves_old);
        auto t1 = std::chrono::steady_clock::now();
        uint64_t nodes_new = run_perft(pos, depth, &all_moves);
        auto t2 = std::chrono::steady_clock::now();

        double s_old = std::chrono::duration<double>(t1 - t0).count();
        double s_new = std::chrono::duration<double>(t2 - t1).count();

        std::cout << "all_moves_old : " << nodes_old << " nodes in " << s_old << "s ("
                  << (nodes_old / s_old / 1e6) << "M nodes/s)\n";
        std::cout << "all_moves     : " << nodes_new << " nodes in " << s_new << "s ("
                  << (nodes_new / s_new / 1e6) << "M nodes/s)\n";
        std::cout << "speedup: " << (s_old / s_new) << "x\n";
    }

    return 0;
}
