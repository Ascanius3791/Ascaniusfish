// OWNERSHIP=Claude
//
// Stress-tests all_moves() (src/move_generation.cpp, TODO.md item 1) against
// all_moves_old() using a large number of positions from
// initialize_FEN_to::random_position() (ascaniusfish.hpp), rather than the small
// set of curated perft positions in legal_movegen_perft_compare.cpp. This exercises
// piece configurations/counts a hand-picked test suite wouldn't: arbitrary material
// imbalances, multiple queens, kings on unusual squares, etc.
//
// random_position() only fills a raw Board[12] - building a legal-enough BB from it
// (white_move, en_passant=0, castling rights, Zobrist hash) is this file's job.
// Castling rights are inferred with castling_rights() (src/Bitboards.cpp), which
// (despite the name similarity to castling_right_rook_correction_for_col()) checks
// BOTH the king and the relevant rook being on their home squares before granting a
// right - exactly what's needed here since random_position() can place the king
// anywhere, not just e1/e8.
//
// Two tiers, per Ascanius's ask for "a sizable number of positions":
//   Part 1: root-only move-list comparison across a large number (default 20000) of
//           random positions - breadth over material/king-placement diversity.
//   Part 2: a smaller sample (default 500), recursively compared 3 plies deep - catches
//           cascading bugs (promotions feeding into subsequent captures, etc.) that a
//           root-only compare can't see. Depth is kept shallow because random material
//           (several queens is common) makes branching factors far higher than real
//           chess positions.
//
// Build (from repo root, like any other diagnostic - see CLAUDE.md):
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG \
//       -o diagnostics/random_position_stress_test diagnostics/random_position_stress_test.cpp

#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
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

// Builds a BB from a random_position() board: side to move is randomized, en
// passant is cleared (random_position() has no move history to justify one),
// castling rights are inferred from king/rook placement, and the Zobrist hash is
// (re)computed to match.
static BB build_random_BB()
{
    uint64_t Board[12];
    bool pawn = (rand() % 100) < 85;
    bool rook = (rand() % 100) < 85;
    bool knight = (rand() % 100) < 85;
    bool bishop = (rand() % 100) < 85;
    bool queen = (rand() % 100) < 85;
    int num_of_pieces = 2 + rand() % 31; // 2..32
    int max_eval_diff = (rand() % 20 == 0) ? 500 : INT_MAX; // occasionally exercise the tight-threshold retry path

    initialize_FEN_to::random_position(Board, max_eval_diff, num_of_pieces, pawn, rook, knight, bishop, queen);

    BB pos; // default-constructs castle[][]=true, en_passant=0, etc.
    for(int i = 0; i < 12; i++) pos.Board[i] = Board[i];
    pos.white_move = rand() % 2;
    pos.en_passant = 0;
    castling_rights(&pos); // infers rights from actual king/rook placement
    pos.zobrist_hash = Zobrist::compute_Zobrist_Hash(pos);
    return pos;
}

static int g_mismatches = 0;
static long g_nodes_checked = 0;
static long g_skipped_overflow = 0;

static const int BUF = 512; // generous headroom over the real 218-move max, for the
                             // (astronomically unlikely but not impossible) case of
                             // many same-type pieces landing on one side

static void compare_recursive(const BB& pos, int depth)
{
    // Random material lets a piece pseudo-legally land on the enemy king's square
    // (both generators treat the enemy king bitboard as just another capture
    // target, same as real chess engines' move generators before a legality check
    // would normally have already ended the game) - a real game can never reach
    // that, but blind recursion into random positions can. Once a king is gone,
    // __builtin_ctzll(0) in both generators' king-square lookup is undefined
    // behavior, so stop rather than comparing (or recursing past) that node.
    if(__builtin_popcountll(pos.Board[5]) != 1 || __builtin_popcountll(pos.Board[11]) != 1)
    return;

    g_nodes_checked++;
    std::vector<BB> buf_old(BUF), buf_new(BUF);

    auto [n_old, mv_old] = all_moves_old(&pos, buf_old.data(), BUF - 1);
    auto [n_new, mv_new] = all_moves(&pos, buf_new.data(), BUF - 1);

    if(n_old == INT_MAX || n_new == INT_MAX)
    {
        g_skipped_overflow++;
        return; // buffer would have overflowed - not a correctness signal either way
    }

    auto k_old = move_keys(mv_old);
    auto k_new = move_keys(mv_new);

    if(k_old != k_new)
    {
        g_mismatches++;
        std::cout << "MISMATCH (depth=" << depth << ", node #" << g_nodes_checked << "):\n";
        print(const_cast<uint64_t*>(pos.Board));
        std::cout << "  white_move=" << pos.white_move
                  << " castle=[" << pos.castle[0][0] << pos.castle[0][1] << "|" << pos.castle[1][0] << pos.castle[1][1] << "]\n";
        std::cout << "  Board = {";
        for(int i = 0; i < 12; i++) std::cout << pos.Board[i] << "ULL" << (i < 11 ? "," : "");
        std::cout << "};\n";
        std::vector<Key> only_old, only_new;
        std::set_difference(k_old.begin(), k_old.end(), k_new.begin(), k_new.end(), std::back_inserter(only_old));
        std::set_difference(k_new.begin(), k_new.end(), k_old.begin(), k_old.end(), std::back_inserter(only_new));
        for(const Key& k : only_old)
            std::cout << "  only in OLD: " << std::get<0>(k) << "->" << std::get<1>(k) << " (promo=" << std::get<2>(k) << ")\n";
        for(const Key& k : only_new)
            std::cout << "  only in NEW: " << std::get<0>(k) << "->" << std::get<1>(k) << " (promo=" << std::get<2>(k) << ")\n";
        if(g_mismatches > 20)
        {
            std::cout << "  ...too many mismatches, stopping.\n";
            return;
        }
    }

    if(depth <= 1) return;
    for(int i = 0; i < n_old && g_mismatches <= 20; i++)
        compare_recursive(buf_old[i], depth - 1);
}

int main()
{
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    Zobrist zobrist_init = Zobrist();
    srand(12345); // fixed seed - reproducible run

    std::cout << "=== Part 1: root-only move-list comparison, many random positions ===\n";
    {
        const int N = 20000;
        int local_mismatches = 0;
        for(int t = 0; t < N; t++)
        {
            BB pos = build_random_BB();
            g_nodes_checked = 0;
            g_mismatches = 0;
            compare_recursive(pos, 1);
            if(g_mismatches > 0) local_mismatches++;
        }
        std::cout << "checked " << N << " random positions, " << local_mismatches << " had a mismatch"
                  << (local_mismatches == 0 ? " - OK\n" : " - *** FAIL ***\n");
    }

    std::cout << "\n=== Part 2: recursive comparison (3 plies), smaller sample ===\n";
    {
        const int N = 500;
        g_nodes_checked = 0;
        g_mismatches = 0;
        g_skipped_overflow = 0;
        long total_nodes = 0;
        int positions_with_mismatch = 0;
        for(int t = 0; t < N; t++)
        {
            BB pos = build_random_BB();
            int before = g_mismatches;
            g_nodes_checked = 0;
            compare_recursive(pos, 3);
            total_nodes += g_nodes_checked;
            if(g_mismatches > before) positions_with_mismatch++;
        }
        std::cout << "checked " << N << " random positions (" << total_nodes << " nodes total, depth 3), "
                  << positions_with_mismatch << " root positions had a mismatch, "
                  << g_skipped_overflow << " nodes skipped (buffer would overflow - not a correctness signal)"
                  << (g_mismatches == 0 ? " - OK\n" : " - *** FAIL ***\n");
    }

    return g_mismatches == 0 ? 0 : 1;
}
