// OWNERSHIP=Claude
//
// one_move() (src/move_generation.cpp) never got the memo from the all_moves()
// rewrite (TODO.md item 1): it still generates a pseudo-legal candidate for every
// piece and tests each one individually with make-move + in_check(), one call per
// candidate square. all_moves() instead computes, once per node, the checkers
// bitboard (+ a checkmask when in single check), the pinned-piece bitboard (+ a
// per-piece ray mask), and a king danger-square bitboard - then filters every
// candidate destination against those O(1) bitboards instead of independently
// verifying it.
//
// one_move_fast() below is that same rewrite applied to the "is there at least one
// legal move" question that exception_eval() (src/eval.cpp) actually asks: it
// reuses all_moves()'s node-level bitboard prep verbatim (duplicated here since
// all_moves()'s copy - and its between_bb() helper - are file-local to
// src/move_generation.cpp, an Ascanius-owned file this diagnostic doesn't touch),
// then, for each piece type, computes the same destination-square bitboard
// all_moves() would and returns true the instant one is non-empty. No move list,
// no child BB array, no Zobrist updates - the one exception is en passant, which -
// exactly as in all_moves() - is still verified by making the move and calling
// in_check(), since removing two pawns off the same rank can expose a check no
// static pin mask covers, and it's rare enough (<=2 candidates) not to matter.
//
// Tested here against one_move() (kept as the trusted oracle - it predates the
// legal-only rewrite but was never in question) three ways:
//   Part 1: root-only comparison over a large number of random positions.
//   Part 2: recursive comparison, descending through all_moves()'s own (already
//           validated) legal children, so every node visited - including
//           checkmate/stalemate leaves, in-check nodes, pinned positions - gets
//           both one_move() and one_move_fast() called on it and compared.
//   Part 3: a handful of curated FENs (the CPW perft positions already used by
//           legal_movegen_perft_compare.cpp, plus a few textbook
//           checkmate/stalemate positions) run through Part 2's recursive check.
//
// Build (from repo root, like any other diagnostic - see CLAUDE.md):
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG \
//       -o diagnostics/one_move_fast_stress_test diagnostics/one_move_fast_stress_test.cpp

#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// Copied from all_moves()'s file-local helper (src/move_generation.cpp) - that
// copy is `static`, and this whole repo builds as a single translation unit (see
// CLAUDE.md), so `static` alone doesn't avoid a name clash with the original once
// both files are in the same TU; renamed to keep this file link-safe as a
// standalone diagnostic while staying a straight duplicate of the original logic.
static inline uint64_t between_bb_fast(int a, int b)
{
    int ra = a / 8, fa = a % 8;
    int rb = b / 8, fb = b % 8;
    int dr = (rb > ra) - (rb < ra);
    int df = (fb > fa) - (fb < fa);
    if(ra != rb && fa != fb && (ra - rb != fa - fb) && (ra - rb != -(fa - fb)))
        return 0;

    uint64_t bb = 0;
    int r = ra + dr, f = fa + df;
    while(r != rb || f != fb)
    {
        bb |= 1ULL << (r * 8 + f);
        r += dr;
        f += df;
    }
    return bb;
}

bool one_move_fast(const BB* const original)
{
    const bool WM = original->white_move;
    const int OWN = 6 * !WM;
    const int ENE = 6 * WM;

    uint64_t Own_P = 0, Enemy_P = 0;
    for(int i = 0; i < 6; i++)
    {
        Own_P |= original->Board[i + OWN];
        Enemy_P |= original->Board[i + ENE];
    }
    const uint64_t occupancy = Own_P | Enemy_P;

    const int king_sq = __builtin_ctzll(original->Board[5 + OWN]);
    const uint64_t enemy_pawns = original->Board[0 + ENE];
    const uint64_t enemy_knights = original->Board[2 + ENE];
    const uint64_t enemy_rook_or_queen = original->Board[1 + ENE] | original->Board[4 + ENE];
    const uint64_t enemy_bishop_or_queen = original->Board[3 + ENE] | original->Board[4 + ENE];
    const uint64_t enemy_king = original->Board[5 + ENE];

    // ---- checkers + checkmask (once per node, same as all_moves()) ----
    uint64_t checkers = (WM ? BP_template[king_sq] : WP_template[king_sq]) & enemy_pawns;
    checkers |= Kn_template[king_sq] & enemy_knights;
    const uint64_t king_rook_view = get_rook_attacks(king_sq, occupancy);
    const uint64_t king_bishop_view = get_bishop_attacks(king_sq, occupancy);
    checkers |= king_rook_view & enemy_rook_or_queen;
    checkers |= king_bishop_view & enemy_bishop_or_queen;
    checkers |= K_template[king_sq] & enemy_king;

    const int checker_count = __builtin_popcountll(checkers);
    uint64_t checkmask = ~0ULL;
    if(checker_count == 1)
    {
        int checker_sq = __builtin_ctzll(checkers);
        checkmask = checkers;
        if((enemy_rook_or_queen | enemy_bishop_or_queen) & checkers)
            checkmask |= between_bb_fast(king_sq, checker_sq);
    }
    else if(checker_count >= 2)
        checkmask = 0; // double check: only the king can move

    // ---- pinned pieces (once per node) ----
    uint64_t pinned = 0;
    uint64_t pin_mask[64];
    for(int i = 0; i < 64; i++) pin_mask[i] = ~0ULL;

    const uint64_t occ_without_own = occupancy & ~Own_P;
    uint64_t pinner_candidates = get_rook_attacks(king_sq, occ_without_own) & enemy_rook_or_queen;
    while(pinner_candidates)
    {
        int p = find_and_delete_trailling_1(pinner_candidates);
        uint64_t between = between_bb_fast(king_sq, p);
        uint64_t blockers = between & Own_P;
        if(__builtin_popcountll(blockers) == 1)
        {
            int b = __builtin_ctzll(blockers);
            pinned |= 1ULL << b;
            pin_mask[b] = between | (1ULL << p);
        }
    }
    pinner_candidates = get_bishop_attacks(king_sq, occ_without_own) & enemy_bishop_or_queen;
    while(pinner_candidates)
    {
        int p = find_and_delete_trailling_1(pinner_candidates);
        uint64_t between = between_bb_fast(king_sq, p);
        uint64_t blockers = between & Own_P;
        if(__builtin_popcountll(blockers) == 1)
        {
            int b = __builtin_ctzll(blockers);
            pinned |= 1ULL << b;
            pin_mask[b] = between | (1ULL << p);
        }
    }

    // ---- king danger squares (once per node) ----
    const uint64_t occ_no_own_king = occupancy & ~(1ULL << king_sq);
    uint64_t danger = 0;
    {
        uint64_t p = enemy_pawns;
        while(p) { int i = find_and_delete_trailling_1(p); danger |= (WM ? WP_template[i] : BP_template[i]); }
        uint64_t n = enemy_knights;
        while(n) { int i = find_and_delete_trailling_1(n); danger |= Kn_template[i]; }
        uint64_t r = enemy_rook_or_queen;
        while(r) { int i = find_and_delete_trailling_1(r); danger |= get_rook_attacks(i, occ_no_own_king); }
        uint64_t bi = enemy_bishop_or_queen;
        while(bi) { int i = find_and_delete_trailling_1(bi); danger |= get_bishop_attacks(i, occ_no_own_king); }
        danger |= K_template[__builtin_ctzll(enemy_king)];
    }

    // ---- king moves: O(1) lookup, checked first since it's the cheapest ----
    if(K_template[king_sq] & ~Own_P & ~danger) return true;

    if(checker_count < 2)
    {
        uint64_t own_knights = original->Board[2 + OWN];
        while(own_knights)
        {
            int i = find_and_delete_trailling_1(own_knights);
            uint64_t targets = Kn_template[i] & ~Own_P & checkmask;
            if(pinned & (1ULL << i)) targets &= pin_mask[i];
            if(targets) return true;
        }

        uint64_t own_bishops = original->Board[3 + OWN] | original->Board[4 + OWN];
        while(own_bishops)
        {
            int i = find_and_delete_trailling_1(own_bishops);
            uint64_t targets = get_bishop_attacks(i, occupancy) & ~Own_P & checkmask;
            if(pinned & (1ULL << i)) targets &= pin_mask[i];
            if(targets) return true;
        }

        uint64_t own_rooks = original->Board[1 + OWN] | original->Board[4 + OWN];
        while(own_rooks)
        {
            int i = find_and_delete_trailling_1(own_rooks);
            uint64_t targets = get_rook_attacks(i, occupancy) & ~Own_P & checkmask;
            if(pinned & (1ULL << i)) targets &= pin_mask[i];
            if(targets) return true;
        }

        uint64_t own_pawns = original->Board[0 + OWN];
        while(own_pawns)
        {
            int i = find_and_delete_trailling_1(own_pawns);
            uint64_t pin = (pinned & (1ULL << i)) ? pin_mask[i] : ~0ULL;

            if(WM)
            {
                if(BP_template[i] & Enemy_P & checkmask & pin) return true;

                if(!(occupancy & (1ULL << (i + 8))))
                {
                    if((checkmask & (1ULL << (i + 8))) && (pin & (1ULL << (i + 8)))) return true;
                    if(i < 16 && !(occupancy & (1ULL << (i + 16))) &&
                       (checkmask & (1ULL << (i + 16))) && (pin & (1ULL << (i + 16)))) return true;
                }

                if(original->en_passant && (BP_template[i] & original->en_passant))
                {
                    int j = __builtin_ctzll(original->en_passant);
                    BB temp;
                    Base_BB(original, &temp);
                    temp.Board[0] &= ~(1ULL << i);
                    temp.Board[6] &= ~((original->en_passant & 1ULL << j) >> 8);
                    clear_sq_of_enemy(temp.Board, j, WM);
                    temp.Board[0] |= 1ULL << j;
                    if(!in_check(temp.Board, WM)) return true;
                }
            }
            else
            {
                if(WP_template[i] & Enemy_P & checkmask & pin) return true;

                if(!(occupancy & (1ULL << (i - 8))))
                {
                    if((checkmask & (1ULL << (i - 8))) && (pin & (1ULL << (i - 8)))) return true;
                    if(i >= 8 * 6 && !(occupancy & (1ULL << (i - 16))) &&
                       (checkmask & (1ULL << (i - 16))) && (pin & (1ULL << (i - 16)))) return true;
                }

                if(original->en_passant && (WP_template[i] & original->en_passant))
                {
                    int j = __builtin_ctzll(original->en_passant);
                    BB temp;
                    Base_BB(original, &temp);
                    temp.Board[6] &= ~(1ULL << i);
                    temp.Board[0] &= ~((original->en_passant & 1ULL << j) << 8);
                    clear_sq_of_enemy(temp.Board, j, WM);
                    temp.Board[6] |= 1ULL << j;
                    if(!in_check(temp.Board, WM)) return true;
                }
            }
        }
    }

    // ---- castling: needs checker_count==0; can be legal even when no other move
    //      is (e.g. every other piece pinned, and the two king-only squares K_template
    //      already ruled out above are both danger squares while the castle dest isn't) ----
    if(checker_count == 0)
    {
        if(original->castle[WM][0] && !((Enemy_P | Own_P) & (0B00001110ULL << (!WM * 7 * 8))))
        {
            int transit = 3 + !WM * 7 * 8, dest = 2 + !WM * 7 * 8;
            if(!(danger & (1ULL << transit)) && !(danger & (1ULL << dest))) return true;
        }
        if(original->castle[WM][1] && !((Enemy_P | Own_P) & (0B01100000ULL << (!WM * 7 * 8))))
        {
            int transit = 5 + !WM * 7 * 8, dest = 6 + !WM * 7 * 8;
            if(!(danger & (1ULL << transit)) && !(danger & (1ULL << dest))) return true;
        }
    }

    return false;
}

// ---------------------------- test harness ----------------------------

static bool kings_ok(const BB& pos)
{
    return __builtin_popcountll(pos.Board[5]) == 1 && __builtin_popcountll(pos.Board[11]) == 1;
}

static BB build_random_BB()
{
    uint64_t Board[12];
    bool pawn = (rand() % 100) < 85;
    bool rook = (rand() % 100) < 85;
    bool knight = (rand() % 100) < 85;
    bool bishop = (rand() % 100) < 85;
    bool queen = (rand() % 100) < 85;
    int num_of_pieces = 2 + rand() % 31;
    int max_eval_diff = (rand() % 20 == 0) ? 500 : INT_MAX;

    initialize_FEN_to::random_position(Board, max_eval_diff, num_of_pieces, pawn, rook, knight, bishop, queen);

    BB pos;
    for(int i = 0; i < 12; i++) pos.Board[i] = Board[i];
    pos.white_move = rand() % 2;
    pos.en_passant = 0;
    castling_rights(&pos);
    pos.zobrist_hash = Zobrist::compute_Zobrist_Hash(pos);
    return pos;
}

static long g_nodes_checked = 0;
static int g_mismatches = 0;
static const int BUF = 512;

static void compare_recursive(const BB& pos, int depth)
{
    if(!kings_ok(pos)) return;

    g_nodes_checked++;
    bool old_has_move = one_move(&pos);
    bool new_has_move = one_move_fast(&pos);
    if(old_has_move != new_has_move)
    {
        g_mismatches++;
        std::cout << "MISMATCH (depth=" << depth << ", node #" << g_nodes_checked << "): "
                  << "one_move=" << old_has_move << " one_move_fast=" << new_has_move << "\n";
        print(const_cast<uint64_t*>(pos.Board));
        std::cout << "  white_move=" << pos.white_move
                  << " castle=[" << pos.castle[0][0] << pos.castle[0][1] << "|" << pos.castle[1][0] << pos.castle[1][1] << "]"
                  << " en_passant=" << pos.en_passant << "\n";
        std::cout << "  Board = {";
        for(int i = 0; i < 12; i++) std::cout << pos.Board[i] << "ULL" << (i < 11 ? "," : "");
        std::cout << "};\n";
        if(g_mismatches > 20)
        {
            std::cout << "  ...too many mismatches, stopping.\n";
            return;
        }
    }

    if(depth <= 0 || !old_has_move) return;

    std::vector<BB> children(BUF);
    auto [n, mv] = all_moves(&pos, children.data(), BUF - 1);
    if(n == INT_MAX) return; // buffer would overflow - not a correctness signal

    for(int i = 0; i < n && g_mismatches <= 20; i++)
        compare_recursive(children[i], depth - 1);
}

int main()
{
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    Zobrist zobrist_init = Zobrist();
    srand(424242);

    int total_mismatches = 0;

    std::cout << "=== Part 1: root-only comparison, many random positions ===\n";
    {
        const int N = 50000;
        int local_mismatches = 0;
        for(int t = 0; t < N; t++)
        {
            BB pos = build_random_BB();
            if(!kings_ok(pos)) continue;
            bool old_has_move = one_move(&pos);
            bool new_has_move = one_move_fast(&pos);
            if(old_has_move != new_has_move)
            {
                local_mismatches++;
                std::cout << "MISMATCH: one_move=" << old_has_move << " one_move_fast=" << new_has_move << "\n";
                print(const_cast<uint64_t*>(pos.Board));
            }
        }
        std::cout << "checked " << N << " random positions, " << local_mismatches << " had a mismatch"
                  << (local_mismatches == 0 ? " - OK\n" : " - *** FAIL ***\n");
        total_mismatches += local_mismatches;
    }

    std::cout << "\n=== Part 2: recursive comparison through all_moves()'s legal tree, random roots ===\n";
    {
        // Depth/N mirrors random_position_stress_test.cpp's own Part 2 (already
        // proven safe in this repo): random material can have branching factors far
        // above real chess (several queens is common), so unlike the curated
        // real-game positions in Part 3, going one ply deeper here risks a
        // combinatorial blowup - discovered the hard way when depth 4 drove this
        // process's RSS past 1.5GB in ~7s and it got OOM-killed. That blowup also
        // exposed a real bug worth flagging separately: one_move() (src/move_generation.cpp)
        // does `new BB` on every call and never deletes it, so any stress test (or,
        // more worryingly, any real search - exception_eval() calls one_move() at
        // every leaf) leaks sizeof(BB)=136 bytes per call.
        const int N = 500;
        int depth = 3;
        long total_nodes = 0;
        int positions_with_mismatch = 0;
        for(int t = 0; t < N; t++)
        {
            BB pos = build_random_BB();
            g_nodes_checked = 0;
            int before = g_mismatches;
            compare_recursive(pos, depth);
            total_nodes += g_nodes_checked;
            if(g_mismatches > before) positions_with_mismatch++;
        }
        std::cout << "checked " << N << " random roots (" << total_nodes << " nodes total, depth " << depth << "), "
                  << positions_with_mismatch << " roots had a mismatch"
                  << (positions_with_mismatch == 0 ? " - OK\n" : " - *** FAIL ***\n");
        total_mismatches += (g_mismatches);
    }

    std::cout << "\n=== Part 3: curated positions (CPW perft set + textbook mates/stalemates) ===\n";
    {
        struct TestPosition { std::string name; std::string fen; int depth; };
        std::vector<TestPosition> positions = {
            {"startpos",  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
            {"kiwipete",  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
            {"position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4},
            {"position4", "r3k2r/Pppp1ppp/1B3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3},
            {"position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 0 1", 3},
            {"position6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 1", 3},
            // Fool's mate: 1.f3 e5 2.g4 Qh4# - white has no legal move, in check (checkmate).
            {"fools_mate", "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3", 0},
            // Scholar's mate: 1.e4 e5 2.Bc4 Nc6 3.Qh5 Nf6?? 4.Qxf7# - black has no legal move (checkmate).
            {"scholars_mate", "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4", 0},
            // Textbook stalemate: black king a8 boxed in by white king b6 and queen c7, black to move, not in check.
            {"stalemate", "k7/2Q5/1K6/8/8/8/8/8 b - - 0 1", 0},
        };
        for(const auto& tp : positions)
        {
            BB pos(tp.fen);
            g_mismatches = 0;
            g_nodes_checked = 0;
            compare_recursive(pos, tp.depth);
            std::cout << tp.name << ": checked " << g_nodes_checked << " nodes to depth " << tp.depth
                      << ", mismatches=" << g_mismatches << (g_mismatches == 0 ? " OK\n" : " *** FAIL ***\n");
            total_mismatches += g_mismatches;
        }
    }

    std::cout << "\n" << (total_mismatches == 0 ? "ALL OK\n" : "*** FAILURES FOUND ***\n");
    return total_mismatches == 0 ? 0 : 1;
}
