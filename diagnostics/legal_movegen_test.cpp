// OWNERSHIP=Claude
//
// Checks generate_legal_moves(), count_legal_moves() and make_move()
// (src/move_generation.cpp, issue #34) against all_moves(), which stays the
// reference. At every node visited:
//   - GEN_ALL gives all_moves()'s moves, same order, every Move field equal;
//   - GEN_CAPTURES and GEN_QUIETS give the capture/promotion and the other moves
//     of that list, each in its order;
//   - count_legal_moves() gives the count, and min(count,limit) for limits 1-3;
//   - make_move() gives all_moves()'s child for each move, every BB field equal
//     (boards, side, castling, en passant, hash, counters), and a hash that
//     matches compute_Zobrist_Hash().
// Nodes come from three places:
//   1. the perft suite of tools/perft.cpp, every node to a fixed depth;
//   2. random games from those positions, every node along each game;
//   3. random_position() boards (see diagnostics/random_position_stress_test.cpp),
//      the root only: they can hold pieces attacking the king of the side that
//      just moved, which real play never reaches, so their children are not walked.
// Then times perft(startpos) with all_moves() and with the new pair.
//
//   ./diagnostics/legal_movegen_test [perft_depth=4] [random_games=4000] [random_roots=20000]
//
// Build (from repo root, like any other diagnostic - see CLAUDE.md):
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -pthread -o diagnostics/legal_movegen_test diagnostics/legal_movegen_test.cpp

#include "../lib/uci.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

static const char* SUITE[][2] =
{
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
    {"pos3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
    {"pos4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
    {"pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"},
    {"pos6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
};

static const int BUF = MOVE_LIST_CAP + 8; // all_moves() may write 3 boards past its count at a promotion
static const int MAX_DEPTH = 16;
static BB bufs[MAX_DEPTH][BUF];

static long long g_nodes = 0;
static long long g_moves = 0;
static int g_failures = 0;
// how often the walk met what the masks have to get right
static long long g_en_passant = 0, g_castling = 0, g_promotions = 0, g_in_check = 0, g_double_check = 0;

static bool same_move(const Move& a, const Move& b)
{
    return a.from==b.from && a.to==b.to && a.promotion_piece_type==b.promotion_piece_type
        && a.is_castling==b.is_castling && a.is_en_passant==b.is_en_passant;
}

static bool same_child(const BB& a, const BB& b)
{
    for(int i=0;i<12;i++)
    if(a.Board[i]!=b.Board[i])
    return false;
    return a.white_move==b.white_move
        && a.castle[0][0]==b.castle[0][0] && a.castle[0][1]==b.castle[0][1]
        && a.castle[1][0]==b.castle[1][0] && a.castle[1][1]==b.castle[1][1]
        && a.en_passant==b.en_passant && a.zobrist_hash==b.zobrist_hash
        && a.move==b.move
        && a.halfmoves_since_last_capture_or_pawn_move==b.halfmoves_since_last_capture_or_pawn_move
        && a.number_of_repetitions==b.number_of_repetitions;
}

static std::string move_str(const Move& m)
{
    char s[64];
    std::snprintf(s, sizeof s, "%c%d%c%d p%d c%d ep%d", 'a'+m.from%8, 1+m.from/8, 'a'+m.to%8, 1+m.to/8,
                  m.promotion_piece_type, m.is_castling, m.is_en_passant);
    return s;
}

static void fail(const BB& pos, const std::string& what)
{
    g_failures++;
    if(g_failures<=20)
    {
        BB copy = pos;
        std::printf("FAIL %s\n  fen %s\n", what.c_str(), copy.get_FEN().c_str());
    }
}

static bool is_capture_or_promotion(const BB& pos, const Move& m)
{
    const uint64_t enemy = pos.get_pieces_of_colour(!pos.white_move);
    return m.is_en_passant || (enemy >> m.to & 1) || m.promotion_piece_type>=1;
}

// Checks one node; returns all_moves()'s move count, its children in bufs[ply].
static int check_node(const BB& pos, int ply)
{
    g_nodes++;
    BB* const children = bufs[ply];
    auto [n, moves] = all_moves(&pos, children);
    {
        const bool WM = pos.white_move;
        const int k = __builtin_ctzll(pos.Board[5+6*!WM]);
        uint64_t occ = pos.get_occupancy();
        const int ENE = 6*WM;
        const uint64_t checkers = ((WM ? BP_template[k] : WP_template[k]) & pos.Board[ENE]) | (Kn_template[k] & pos.Board[2+ENE])
            | (get_rook_attacks(k,occ) & (pos.Board[1+ENE]|pos.Board[4+ENE])) | (get_bishop_attacks(k,occ) & (pos.Board[3+ENE]|pos.Board[4+ENE]));
        g_in_check += checkers!=0;
        g_double_check += __builtin_popcountll(checkers)>=2;
        for(int i=0;i<n;i++)
        {
            g_en_passant += moves[i].is_en_passant;
            g_promotions += moves[i].promotion_piece_type>=1;
            g_castling += !(pos.get_pieces_of_colour(WM) >> moves[i].from & 1);
        }
    }

    Move_List all;
    const int n_new = generate_legal_moves<GEN_ALL>(&pos, all);
    if(n_new!=n || all.size!=n)
    {
        fail(pos, "GEN_ALL count " + std::to_string(n_new) + " vs all_moves " + std::to_string(n));
        return n;
    }
    for(int i=0;i<n;i++)
    if(!same_move(all[i], moves[i]))
    {
        fail(pos, "move " + std::to_string(i) + ": " + move_str(all[i]) + " vs all_moves " + move_str(moves[i]));
        return n;
    }

    // the two modes: each the matching subsequence of all_moves()'s order
    Move_List caps, quiets;
    caps.size = 3;//appends after what is already there
    generate_legal_moves(&pos, caps, GEN_CAPTURES);
    generate_legal_moves<GEN_QUIETS>(&pos, quiets);
    int ci = 3, qi = 0;
    for(int i=0;i<n;i++)
    {
        Move_List& list = is_capture_or_promotion(pos, moves[i]) ? caps : quiets;
        int& k = &list==&caps ? ci : qi;
        if(k>=list.size || !same_move(list[k], moves[i]))
        {
            fail(pos, std::string(&list==&caps ? "GEN_CAPTURES" : "GEN_QUIETS") + " at " + move_str(moves[i]));
            return n;
        }
        k++;
    }
    if(ci!=caps.size || qi!=quiets.size)
    fail(pos, "mode lists longer than all_moves()'s");

    if(count_legal_moves(&pos)!=n)
    fail(pos, "count_legal_moves " + std::to_string(count_legal_moves(&pos)) + " vs " + std::to_string(n));
    for(int limit=1;limit<=3;limit++)
    if(count_legal_moves(&pos, limit)!=std::min(n, limit))
    fail(pos, "count_legal_moves limit " + std::to_string(limit));

    for(int i=0;i<n;i++)
    {
        BB child;
        make_move(&pos, moves[i], &child);
        g_moves++;
        if(!same_child(child, children[i]))
        {
            fail(pos, "make_move child differs for " + move_str(moves[i]));
            return n;
        }
        if(child.zobrist_hash!=Zobrist::compute_Zobrist_Hash(child))
        fail(pos, "make_move hash != compute_Zobrist_Hash for " + move_str(moves[i]));
    }
    return n;
}

static void walk(const BB& pos, int depth, int ply)
{
    const int n = check_node(pos, ply);
    if(depth<=1)
    return;
    for(int i=0;i<n;i++)
    walk(bufs[ply][i], depth-1, ply+1);
}

static void random_game(BB pos, std::mt19937& rng, int max_plies)
{
    for(int ply=0; ply<max_plies; ply++)
    {
        const int n = check_node(pos, 0);
        if(n==0)
        return;
        pos = bufs[0][rng()%n];
    }
}

// As in diagnostics/random_position_stress_test.cpp: random material, castling
// rights where king and rook stand at home, no en passant.
static BB random_root()
{
    uint64_t Board[12];
    initialize_FEN_to::random_position(Board, INT_MAX, 2 + rand()%31,
        rand()%100<85, rand()%100<85, rand()%100<85, rand()%100<85, rand()%100<85);
    BB pos;
    for(int i=0;i<12;i++)
    pos.Board[i]=Board[i];
    pos.white_move = rand()%2;
    pos.en_passant = 0;
    castling_rights(&pos);
    pos.zobrist_hash = Zobrist::compute_Zobrist_Hash(pos);
    return pos;
}

static long long perft_old(const BB* pos, int depth, BB* buf)
{
    const int n = std::get<0>(all_moves(pos, buf));
    if(depth<=1)
    return n;
    long long nodes = 0;
    for(int i=0;i<n;i++)
    nodes += perft_old(buf+i, depth-1, buf+n);
    return nodes;
}

static long long perft_new(const BB* pos, int depth)
{
    if(depth==0)
    return 1;
    Move_List list;
    const int n = generate_legal_moves<GEN_ALL>(pos, list);
    long long nodes = 0;
    BB child;
    for(int i=0;i<n;i++)
    {
        make_move(pos, list[i], &child);
        nodes += perft_new(&child, depth-1);
    }
    return nodes;
}

template<class F>
static double seconds(F f)
{
    auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
}

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    const int perft_depth = argc>1 ? std::atoi(argv[1]) : 4;
    const int random_games = argc>2 ? std::atoi(argv[2]) : 4000;
    const int random_roots = argc>3 ? std::atoi(argv[3]) : 20000;

    for(auto& c : SUITE)
    {
        BB root;
        if(!uci_parse_fen(c[1], root))
        {
            std::printf("%s: invalid FEN\n", c[0]);
            return 1;
        }
        const long long before = g_nodes;
        walk(root, perft_depth, 0);
        std::printf("suite %-9s depth %d: %lld nodes checked\n", c[0], perft_depth, g_nodes-before);
    }

    std::mt19937 rng(34);
    long long before = g_nodes;
    for(int g=0; g<random_games; g++)
    {
        BB root;
        uci_parse_fen(SUITE[g%6][1], root);
        random_game(root, rng, 300);
    }
    std::printf("random games: %d games, %lld nodes checked\n", random_games, g_nodes-before);

    srand(34);
    before = g_nodes;
    for(int r=0; r<random_roots; r++)
    check_node(random_root(), 0);
    std::printf("random roots: %lld nodes checked\n", g_nodes-before);

    std::printf("met: %lld en passant, %lld castling, %lld promotion moves; %lld nodes in check, %lld in double check\n",
                g_en_passant, g_castling, g_promotions, g_in_check, g_double_check);
    std::printf("total: %lld nodes, %lld children -> %d failures\n", g_nodes, g_moves, g_failures);

    BB start;
    uci_parse_fen(SUITE[0][1], start);
    long long n_old = 0, n_new = 0;
    static BB perft_buf[1<<12];
    const double t_old = seconds([&]{ n_old = perft_old(&start, 5, perft_buf); });
    const double t_new = seconds([&]{ n_new = perft_new(&start, 5); });
    std::printf("perft(startpos,5): all_moves %lld in %.2f s (%.1f Mnps), generate_legal_moves+make_move (every leaf made too) %lld in %.2f s (%.1f Mnps)\n",
                n_old, t_old, n_old/t_old/1e6, n_new, t_new, n_new/t_new/1e6);
    if(n_old!=n_new)
    g_failures++;

    return g_failures ? 1 : 0;
}
