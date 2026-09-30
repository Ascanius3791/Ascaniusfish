// OWNERSHIP=Claude
// Plan eval (#43/#44) cost/gain numbers for candidate improvements. A local,
// instrumented copy of plan_eval_impl() (src/plan_eval.cpp) with compile-time
// switches, measured on the probe's position set (tools/openings.epd + 60-ply
// random playouts, same seed) plus, per position, one random capture child
// where there is one (quiescence leaves are capture-heavy):
//   1. time: basic_eval with/without the term, the term split into setup /
//      BFS / db evaluation, targets evaluated per call and piece type
//   2. value per piece type: mean |term|, share, chosen N, no-target fraction
//   3. early stop: a safe per-piece upper bound UB on db (verified exact) and
//      an empirical one (max db seen), levels/targets saved and speed
//   4. N capped at 1/2/3   5. cheaper db (own role terms only)
//   6. stability parent->child (what an incremental update could skip)
//   7. |plan_eval| distribution (lazy-eval margin)
//   8. quality variant: also avoid squares attacked by lower-valued enemies
// Exit code 1 when the copy disagrees with plan_eval_detail() or the safe
// early stop is not exact.
//
// Build from the repo root:
//   g++ -O3 -mpopcnt -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/plan_eval_stats diagnostics/plan_eval_stats.cpp
//   flock -w 3600 /tmp/ascaniusfish-heavy.lock ./diagnostics/plan_eval_stats
#include "../lib/uci.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

// ---------------------------------------------------------------------------
// the instrumented copy

struct Piece_Rec
{
    int piece, from, to, n, db, term;
    int ub;          // safe upper bound on db (UB_SAFE / UB_CHECK only)
    int levels;      // BFS levels expanded (non-empty ones)
    int targets;     // targets whose db was evaluated
    uint64_t reached;
    int db_at[64];   // db of every evaluated target (reached squares only)
};

struct Rec
{
    int n;
    Piece_Rec pc[32];
};

struct Counters
{
    long long targets[6], levels[6], pieces[6];
    long long ub_viol;   // db > safe UB (must stay 0)
    long long stops;     // early stops taken
    long long ub_sum[6], maxdb_sum[6], maxdb_pieces[6];
    long long touched;   // other pieces' roles recomputed, summed over targets
};
static Counters CNT;

static int EMP_UB[6];           // max db seen per piece type (empirical bound)
static int MAX_DB_SEEN[6];

enum { UB_NONE = 0, UB_SAFE = 1, UB_EMP = 2, UB_CHECK = 3 };// CHECK: compute the safe bound, never stop

// Owner's best possible gain per role kind from one square changing: vacating s
// / occupying t, for a role of the mover's own colour or the enemy's. From the
// role terms in plan_role_term(): a pawn role's att/side/front squares are
// +-30/+-20/-+20; a slider's ray gains or loses at most 6 squares (5 or 7 each)
// plus one blocker (own 10/-10, enemy 40); leapers only recount one square.
// The move is split into "vacate s" then "occupy t" (vacating only extends
// masks, and the extra roles seeing t then are among those seeing s), so
// UB = PST part + (own roles' max - now + sum over roles seeing s of S+T
// + max over empty t of the T sum)/2 + pawn support, each +1 for truncation.
// db > UB is counted and must stay 0.
//                                      PAWN DIAG ORTH KNIGHT KING
static const int S_MAX_OWN[5]   = {    20,  60,  92,  10,     0 };
static const int S_MAX_ENEMY[5] = {    30,  40,  43,  10,    20 };
static const int T_MAX_OWN[5]   = {    30,  10,   0,   0,    15 };
static const int T_MAX_ENEMY[5] = {    20,  30,  42,   0,     0 };
static const int ROLE_MAX[5]    = {   100, 205, 258, 120,   160 };// a role's own-view term at any square

static inline int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static int PT_MAX_O[7], PT_MAX_E[7];

static const int PIECE_VALUE[6] = {1, 5, 3, 3, 9, 100};

template<int STAGE, int CAPN, int UBM, bool CHEAP, bool AVOID, bool RECORD>
__attribute__((noinline)) static int plan_variant(const BB* const original, const WEIGHTS& W, Rec* rec)
{
    const uint64_t* Board = original->Board;
    uint64_t side_pieces[2] = {0,0};// [1] = white
    for(int p=0;p<6;p++)
    {
        side_pieces[1] |= Board[p];
        side_pieces[0] |= Board[p+6];
    }
    const uint64_t occupancy = side_pieces[0] | side_pieces[1];
    uint64_t pawn_attacks[2] = {0,0};// [1] = attacked by white pawns
    for(int col=0;col<2;col++)
    {
        uint64_t pawns = Board[6*!col];
        while(pawns)
        {
            int i=find_and_delete_trailling_1(pawns);
            pawn_attacks[col] |= col ? BP_template[i] : WP_template[i];
        }
    }
    const uint64_t last_ranks = mask_row[0] | mask_row[7];

    // AVOID: avoid_for[col][type] = squares attacked by col's pieces worth less than `type`
    uint64_t avoid_for[2][6] = {};
    if constexpr (AVOID)
    {
        for(int col=0;col<2;col++)
        {
            uint64_t att[6] = {pawn_attacks[col],0,0,0,0,0};
            const int e = 6*!col;
            uint64_t bb = Board[2+e];
            while(bb) att[2] |= Kn_template[find_and_delete_trailling_1(bb)];
            bb = Board[3+e];
            while(bb) att[3] |= get_bishop_attacks(find_and_delete_trailling_1(bb),occupancy);
            bb = Board[1+e];
            while(bb) att[1] |= get_rook_attacks(find_and_delete_trailling_1(bb),occupancy);
            bb = Board[4+e];
            while(bb) { const int i = find_and_delete_trailling_1(bb); att[4] |= get_rook_attacks(i,occupancy) | get_bishop_attacks(i,occupancy); }
            for(int t=1;t<6;t++)
            for(int u=0;u<5;u++)
            if(PIECE_VALUE[u] < PIECE_VALUE[t])
            avoid_for[col][t] |= att[u];
        }
    }

    int OW[2], EW[2];
    for(int col=0;col<2;col++)
    {
        const int e = 6*col;
        OW[col] = count(Board[0+e]) + 5*count(Board[1+e]) + 3*count(Board[2+e]) + 3*count(Board[3+e]) + 9*count(Board[4+e]);
        EW[col] = 39 - OW[col];
    }
    int pt39 = 0;
    for(int p=0;p<12;p++)
    {
        const bool white = p < 6;
        const int row = white ? p : (p==6 ? 6 : p-6);
        uint64_t bb = Board[p];
        while(bb)
        {
            const int i = find_and_delete_trailling_1(bb);
            const int v = W.piece_table_value_opening[row][i]*OW[white] + W.piece_table_value_endgame[row][i]*EW[white];
            pt39 += white ? v : -v;
        }
    }

    Plan_Role roles[48];
    int n_roles = 0;
    int act = 0;
    uint64_t roles_seeing[64];
    if constexpr (!CHEAP)
    {
        for(int p=0;p<12;p++)
        {
            int kinds[2];
            const int nk = plan_roles_of(p % 6, kinds);
            uint64_t bb = Board[p];
            while(bb)
            {
                const int i = find_and_delete_trailling_1(bb);
                for(int k=0;k<nk;k++)
                {
                    Plan_Role& r = roles[n_roles++];
                    r.kind = kinds[k];
                    r.sq = i;
                    r.white = p < 6;
                    r.term = plan_role_term(r.kind, r.white, i, occupancy, side_pieces[1], side_pieces[0], &r.mask);
                    act += r.term;
                }
            }
        }
        for(int i=0;i<64;i++) roles_seeing[i] = 0;
        for(int r=0;r<n_roles;r++)
        {
            uint64_t m = roles[r].mask;
            while(m)
            roles_seeing[find_and_delete_trailling_1(m)] |= 1ULL << r;
        }
    }

    // safe bound: the most any t can add through the roles seeing it, per mover colour
    int max_t_gain[2] = {0,0};
    if constexpr (UBM == UB_SAFE || UBM == UB_CHECK)
    {
        int tgain[2][64] = {};
        for(int r=0;r<n_roles;r++)
        {
            uint64_t m = roles[r].mask & ~occupancy;// targets are empty squares
            const int own = T_MAX_OWN[roles[r].kind], en = T_MAX_ENEMY[roles[r].kind];
            const int g1 = roles[r].white ? own : en, g0 = roles[r].white ? en : own;
            while(m)
            {
                const int s = find_and_delete_trailling_1(m);
                tgain[1][s] += g1;
                tgain[0][s] += g0;
            }
        }
        for(int c=0;c<2;c++)
        for(int s=0;s<64;s++)
        max_t_gain[c] = std::max(max_t_gain[c], tgain[c][s]);
    }

    if constexpr (STAGE == 0)
    {
        uint64_t x = 0;
        if constexpr (!CHEAP)
        for(int i=0;i<64;i++) x ^= roles_seeing[i] * (i+1);
        return pt39 + act + (int)(x ^ x >> 32) + max_t_gain[0] + (int)avoid_for[0][5];
    }

    int score = 0;
    int sink = 0;
    int n_out = 0;
    for(int p=0;p<12;p++)
    {
        const bool white = p < 6;
        const int type = p % 6;
        const int sign = white ? 1 : -1;
        const int row = white ? p : (p==6 ? 6 : p-6);
        const int tempo = white == original->white_move ? 1 : 0;
        int kinds[2];
        const int nk = plan_roles_of(type, kinds);
        const uint64_t own_pawns = Board[6*!white];
        uint64_t pieces = Board[p];
        while(pieces)
        {
            const int from = find_and_delete_trailling_1(pieces);
            const uint64_t from_bb = 1ULL << from;
            const uint64_t others = occupancy & ~from_bb;
            uint64_t allowed = ~occupancy;
            if(type == 0)
            allowed &= ~last_ranks;
            else
            allowed &= ~pawn_attacks[!white];
            if constexpr (AVOID)
            allowed &= ~avoid_for[!white][type];

            int own_roles_term = 0;
            uint64_t own_roles = 0;
            if constexpr (!CHEAP)
            {
                for(int r=0;r<n_roles;r++)
                if(roles[r].sq == from)
                {
                    own_roles_term += roles[r].term;
                    own_roles |= 1ULL << r;
                }
            }
            else
            {
                uint64_t unused;
                for(int k=0;k<nk;k++)
                own_roles_term += plan_role_term(kinds[k], white, from, occupancy, side_pieces[1], side_pieces[0], &unused);
            }
            const int pt_from = W.piece_table_value_opening[row][from]*OW[white] + W.piece_table_value_endgame[row][from]*EW[white];
            const uint64_t other_pawns = own_pawns & ~from_bb;
            const uint64_t attackers_from = white ? WP_template[from] : BP_template[from];
            int ps_from = count(other_pawns & attackers_from);
            if(type == 0)
            ps_from += count(side_pieces[white] & (white ? BP_template[from] : WP_template[from]));

            int ub = INT_MAX;
            if constexpr (UBM == UB_SAFE || UBM == UB_CHECK)
            {
                const int pt_max = PT_MAX_O[row]*OW[white] + PT_MAX_E[row]*EW[white];
                const int ub_pt = floordiv(pt_max - pt_from, 39) + 1;
                int g = max_t_gain[white] - sign*own_roles_term;
                for(int k=0;k<nk;k++)
                g += ROLE_MAX[kinds[k]];
                uint64_t touched = roles_seeing[from] & ~own_roles;
                while(touched)
                {
                    const Plan_Role& r = roles[find_and_delete_trailling_1(touched)];
                    g += r.white == white ? S_MAX_OWN[r.kind] + T_MAX_OWN[r.kind] : S_MAX_ENEMY[r.kind] + T_MAX_ENEMY[r.kind];
                }
                const int ub_act = floordiv(g, 2) + 1;
                const int ub_ps = W.pawn_supporting_value * ((type == 0 ? 4 : 2) - ps_from);
                ub = ub_pt + ub_act + ub_ps;
            }
            else if constexpr (UBM == UB_EMP)
            ub = EMP_UB[type];

            Plan_Target best = {p, from, -1, 0, 0, 0};
            uint64_t reached = from_bb;
            uint64_t frontier = reached;
            int levels = 0, n_targets = 0;
            Piece_Rec* pr = nullptr;
            if constexpr (RECORD)
            {
                pr = &rec->pc[n_out];
                for(int i=0;i<64;i++) pr->db_at[i] = INT_MIN;
            }
            for(int n=1; frontier; n++)
            {
                if constexpr (CAPN > 0)
                if(n > CAPN)
                break;
                if constexpr (UBM == UB_SAFE || UBM == UB_EMP)
                {
                    if(best.term >= ub / (2 + n - tempo))// best.term is 0 while there is no target
                    {
                        if constexpr (RECORD) CNT.stops++;
                        break;
                    }
                }
                uint64_t next = 0;
                while(frontier)
                next |= plan_step(type, white, find_and_delete_trailling_1(frontier), others);
                next &= allowed & ~reached;
                reached |= next;
                frontier = next;
                if(next) levels++;
                if constexpr (STAGE == 1)
                {
                    sink += count(next);
                    continue;
                }
                const int divisor = 2 + n - tempo;
                uint64_t targets = next;
                while(targets)
                {
                    const int to = find_and_delete_trailling_1(targets);
                    const uint64_t move = from_bb | (1ULL << to);
                    const int pt_to = W.piece_table_value_opening[row][to]*OW[white] + W.piece_table_value_endgame[row][to]*EW[white];
                    const int pt_after = pt39 + sign*(pt_to - pt_from);
                    int d = pt_after/39 - pt39/39;
                    const uint64_t occ2 = occupancy ^ move;
                    uint64_t pieces2[2] = {side_pieces[0], side_pieces[1]};
                    pieces2[white] ^= move;
                    uint64_t unused;
                    int db;
                    if constexpr (CHEAP)
                    {
                        int new_t = 0;
                        for(int k=0;k<nk;k++)
                        new_t += plan_role_term(kinds[k], white, to, occ2, pieces2[1], pieces2[0], &unused);
                        d += (new_t - own_roles_term)/2;
                        db = sign*d;
                    }
                    else
                    {
                        int act_after = act - own_roles_term;
                        for(int k=0;k<nk;k++)
                        act_after += plan_role_term(kinds[k], white, to, occ2, pieces2[1], pieces2[0], &unused);
                        uint64_t touched = (roles_seeing[from] | roles_seeing[to]) & ~own_roles;
                        while(touched)
                        {
                            const Plan_Role& r = roles[find_and_delete_trailling_1(touched)];
                            act_after += plan_role_term(r.kind, r.white, r.sq, occ2, pieces2[1], pieces2[0], &unused) - r.term;
                            if constexpr (RECORD) CNT.touched++;
                        }
                        d += act_after/2 - act/2;
                        int ps_to = count(other_pawns & (white ? WP_template[to] : BP_template[to]));
                        if(type == 0)
                        ps_to += count(pieces2[white] & (white ? BP_template[to] : WP_template[to]));
                        db = sign*d + W.pawn_supporting_value*(ps_to - ps_from);
                    }
                    if constexpr (RECORD)
                    {
                        n_targets++;
                        pr->db_at[to] = db;
                        if(db > MAX_DB_SEEN[type]) MAX_DB_SEEN[type] = db;
                        if constexpr (UBM == UB_CHECK)
                        if(db > ub) CNT.ub_viol++;
                    }
                    if(db <= 0)
                    continue;
                    const int term = db / divisor;
                    if(best.to == -1 || term > best.term)
                    best = {p, from, to, n, db, term};
                }
            }
            score += sign * best.term;
            if constexpr (RECORD)
            {
                pr->piece = p; pr->from = from; pr->to = best.to; pr->n = best.n; pr->db = best.db; pr->term = best.term;
                pr->ub = ub; pr->levels = levels; pr->targets = n_targets; pr->reached = reached;
                CNT.targets[type] += n_targets;
                CNT.levels[type] += levels;
                CNT.pieces[type]++;
                int mx = 0;
                for(int i=0;i<64;i++) if(pr->db_at[i] != INT_MIN) mx = std::max(mx, pr->db_at[i]);
                if constexpr (UBM == UB_CHECK) CNT.ub_sum[type] += ub;
                CNT.maxdb_sum[type] += mx;
                CNT.maxdb_pieces[type]++;
            }
            n_out++;
        }
    }
    if constexpr (RECORD)
    rec->n = n_out;
    if constexpr (STAGE == 1)
    return sink;
    return score;
}

// basic_eval() without the plan term (the same calls, src/basic_eval.cpp)
__attribute__((noinline)) static int basic_eval_noplan(const BB* const original, const WEIGHTS& W)
{
    int score = 0;
    score += material_eval(original,W);
    score += piecetable(original,W);
    score += king_safety_eval(original);
    score += positional_eval(original,W);
    score += 5*(count(original->get_attacked_squares(1))-count(original->get_attacked_squares(0)));
    score += piece_activity_eval(original,W);
    return score;
}

__attribute__((noinline)) static int basic_eval_lib(const BB* const p, const WEIGHTS& W) { return basic_eval(p,W); }
__attribute__((noinline)) static int plan_eval_lib(const BB* const p, const WEIGHTS& W) { return plan_eval_detail(p,W,nullptr,nullptr,false); }

// ---------------------------------------------------------------------------
// positions

static BB from_fen(const std::string& fen)
{
    BB pos;
    if(!uci_parse_fen(fen, pos))
    {
        std::printf("bad FEN: %s\n", fen.c_str());
        std::exit(2);
    }
    return pos;
}

static int pieces_of(const BB& b, bool white)
{
    int c = 0;
    for(int p=0;p<6;p++) c += count(b.Board[p + 6*!white]);
    return c;
}

static unsigned rng_state = 12345;
static unsigned rnd() { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 16; }

// ---------------------------------------------------------------------------

struct Stats
{
    double n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sad = 0;
    void add(double x, double y) { n++; sx += x; sy += y; sxx += x*x; syy += y*y; sxy += x*y; sad += std::fabs(x-y); }
    double corr() const { double cx = sxx - sx*sx/n, cy = syy - sy*sy/n; return (sxy - sx*sy/n) / std::sqrt(cx*cy); }
    double mad() const { return sad / n; }
};

typedef int (*Eval_Fn)(const BB* const, const WEIGHTS&);

template<int STAGE, int CAPN, int UBM, bool CHEAP, bool AVOID>
static int fast(const BB* const p, const WEIGHTS& W) { return plan_variant<STAGE,CAPN,UBM,CHEAP,AVOID,false>(p,W,nullptr); }

template<int STAGE, int CAPN, int UBM, bool CHEAP, bool AVOID>
static int recd(const BB* const p, Rec* r) { return plan_variant<STAGE,CAPN,UBM,CHEAP,AVOID,true>(p,WEIGHTS_OG,r); }

struct Timed { const char* name; Eval_Fn f; double best_us; };

static const char TYPE_CH[7] = "PRNBQK";

int main()
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    for(int r=0;r<7;r++)
    {
        PT_MAX_O[r] = PT_MAX_E[r] = INT_MIN;
        for(int s=0;s<64;s++)
        {
            PT_MAX_O[r] = std::max(PT_MAX_O[r], WEIGHTS_OG.piece_table_value_opening[r][s]);
            PT_MAX_E[r] = std::max(PT_MAX_E[r], WEIGHTS_OG.piece_table_value_endgame[r][s]);
        }
    }

    // ---- positions: the probe's set, plus one random capture child each
    std::vector<std::string> fens;
    std::ifstream epd("tools/openings.epd");
    std::string line;
    while(std::getline(epd, line))
    {
        if(line.empty() || line[0] == '#')
        continue;
        std::istringstream in(line);
        std::string a, b, c, d;
        in >> a >> b >> c >> d;
        fens.push_back(a + " " + b + " " + c + " " + d + " 0 1");
    }
    if(fens.empty()) { std::printf("tools/openings.epd not found (run from the repo root)\n"); return 2; }
    BB* wfh = new BB[256];
    std::vector<BB> base;
    std::vector<int> line_of;// which opening's playout a base position is on
    unsigned rng = 12345;
    for(const std::string& fen : fens)
    {
        BB pos = from_fen(fen);
        for(int ply = 0; ply < 60; ply++)
        {
            base.push_back(pos);
            line_of.push_back((int)(&fen - &fens[0]));
            int n = std::get<0>(all_moves(&pos, wfh));
            if(n == 0)
            break;
            rng = rng * 1103515245u + 12345u;
            pos = wfh[(rng >> 16) % n];
        }
    }
    std::vector<BB> caps;
    for(const BB& pos : base)
    {
        int n = std::get<0>(all_moves(&pos, wfh));
        int idx[256], nc = 0;
        const int before = pieces_of(pos, !pos.white_move);
        for(int i=0;i<n;i++)
        if(pieces_of(wfh[i], !pos.white_move) < before)
        idx[nc++] = i;
        if(nc)
        caps.push_back(wfh[idx[rnd() % nc]]);
    }
    std::vector<BB> positions = base;
    positions.insert(positions.end(), caps.begin(), caps.end());
    const size_t NP = positions.size();
    std::printf("positions: %zu playout + %zu capture children = %zu\n", base.size(), caps.size(), NP);

    // ---- the copy equals the library
    {
        int bad = 0;
        for(const BB& pos : positions)
        {
            Plan_Target t[32]; int nt = 0;
            Rec r;
            const int lib = plan_eval_detail(&pos, WEIGHTS_OG, t, &nt);
            const int mine = recd<2,0,UB_CHECK,false,false>(&pos, &r);
            bool same = lib == mine && nt == r.n;
            for(int i=0; same && i<nt; i++)
            same = t[i].to == r.pc[i].to && t[i].n == r.pc[i].n && t[i].db == r.pc[i].db && t[i].term == r.pc[i].term;
            if(!same) bad++;
        }
        std::printf("copy == plan_eval_detail(): %s (%d/%zu differ)\n", bad ? "FAIL" : "ok", bad, NP);
        if(bad) failures++;
    }

    // ---- full recorded pass: items 1 (counts), 2, 3 (bounds), 7
    CNT = Counters{};
    volatile int vsink0 = 0;
    long long type_pieces[6] = {}, type_notarget[6] = {}, type_n[6][5] = {};
    double type_absterm[6] = {};
    std::vector<int> full_score(NP), absplan(NP);
    std::vector<Rec> full_rec_small;// not kept: 8 kB each
    std::vector<int> full_to;        // chosen target per piece, flattened with offsets
    std::vector<int> full_off(NP+1);
    std::vector<int> full_term;
    for(size_t k=0;k<NP;k++)
    {
        Rec r;
        full_score[k] = recd<2,0,UB_CHECK,false,false>(&positions[k], &r);
        absplan[k] = std::abs(full_score[k]);
        full_off[k] = full_to.size();
        for(int i=0;i<r.n;i++)
        {
            const Piece_Rec& p = r.pc[i];
            const int t = p.piece % 6;
            type_pieces[t]++;
            full_to.push_back(p.to);
            full_term.push_back(p.term);
            if(p.to < 0) { type_notarget[t]++; continue; }
            type_absterm[t] += p.term;
            type_n[t][std::min(p.n,4)]++;
        }
    }
    full_off[NP] = full_to.size();
    for(int t=0;t<6;t++) EMP_UB[t] = MAX_DB_SEEN[t];

    std::printf("\n[1b] targets evaluated per call: %.1f total;", (double)(CNT.targets[0]+CNT.targets[1]+CNT.targets[2]+CNT.targets[3]+CNT.targets[4]+CNT.targets[5]) / NP);
    for(int t=0;t<6;t++) std::printf(" %c %.1f", TYPE_CH[t], CNT.targets[t] / (double)NP);
    std::printf("\n     per piece: targets / BFS levels:");
    for(int t=0;t<6;t++) std::printf(" %c %.1f/%.2f", TYPE_CH[t], CNT.targets[t] / (double)CNT.pieces[t], CNT.levels[t] / (double)CNT.pieces[t]);
    std::printf("\n     pieces per call: %.1f; other pieces' roles recomputed per target: %.2f\n", (double)full_to.size() / NP,
                CNT.touched / (double)(CNT.targets[0]+CNT.targets[1]+CNT.targets[2]+CNT.targets[3]+CNT.targets[4]+CNT.targets[5]));

    double all_abs = 0;
    for(int t=0;t<6;t++) all_abs += type_absterm[t];
    std::printf("\n[2] type  pieces  mean|term|  share  noTarget   N=1    N=2    N=3    N=4+\n");
    for(int t=0;t<6;t++)
    {
        const long long wt = type_pieces[t] - type_notarget[t];
        std::printf("    %c    %7lld  %8.2f  %5.1f%%   %5.1f%%  %5.1f%% %5.1f%% %5.1f%% %5.1f%%\n", TYPE_CH[t], type_pieces[t],
                    type_absterm[t] / type_pieces[t], 100 * type_absterm[t] / all_abs, 100.0 * type_notarget[t] / type_pieces[t],
                    100.0 * type_n[t][1] / wt, 100.0 * type_n[t][2] / wt, 100.0 * type_n[t][3] / wt, 100.0 * type_n[t][4] / wt);
    }

    // [3] bounds
    std::printf("\n[3] type  max db seen  mean max-db/piece  mean safe UB/piece   static PST part (max-min, cp)  own-role range\n");
    static const char* ROLE_RANGE[6] = {"[-20,100]", "[-12,258]", "[-40,120]", "[5,205]", "[-7,463]", "[0,160]"};
    for(int t=0;t<6;t++)
    {
        int ro = 0, re = 0;
        const int row = t;
        int mnO = INT_MAX, mxO = INT_MIN, mnE = INT_MAX, mxE = INT_MIN;
        for(int s=0;s<64;s++)
        {
            if(t == 0 && (s < 8 || s >= 56)) continue;
            mnO = std::min(mnO, WEIGHTS_OG.piece_table_value_opening[row][s]); mxO = std::max(mxO, WEIGHTS_OG.piece_table_value_opening[row][s]);
            mnE = std::min(mnE, WEIGHTS_OG.piece_table_value_endgame[row][s]); mxE = std::max(mxE, WEIGHTS_OG.piece_table_value_endgame[row][s]);
        }
        ro = mxO - mnO; re = mxE - mnE;
        std::printf("    %c    %6d        %8.1f          %8.1f             %4d (open) %4d (end)        %s\n", TYPE_CH[t], MAX_DB_SEEN[t],
                    CNT.maxdb_sum[t] / (double)CNT.maxdb_pieces[t], CNT.ub_sum[t] / (double)CNT.pieces[t], ro, re, ROLE_RANGE[t]);
    }
    std::printf("    db > safe UB: %lld times (must be 0)\n", CNT.ub_viol);
    if(CNT.ub_viol) failures++;

    // [7]
    {
        std::vector<int> a = absplan;
        std::sort(a.begin(), a.end());
        double m = 0; for(int x : a) m += x;
        std::printf("\n[7] |plan_eval| cp: mean %.1f  median %d  p90 %d  p95 %d  p99 %d  max %d\n", m / NP, a[NP/2], a[NP*90/100], a[NP*95/100], a[NP*99/100], a.back());
        // also over the capture children only (quiescence-like)
        std::vector<int> c(absplan.begin() + base.size(), absplan.end());
        std::sort(c.begin(), c.end());
        double mc = 0; for(int x : c) mc += x;
        if(!c.empty())
        std::printf("    capture children only: mean %.1f  p95 %d  p99 %d  max %d\n", mc / c.size(), c[c.size()*95/100], c[c.size()*99/100], c.back());
    }

    // ---- variants compared with the full term (records)
    auto compare = [&](const char* name, auto fn, bool exact_expected)
    {
        CNT = Counters{};
        Stats st;
        long long piece_diff = 0, pieces = 0, term_diff = 0, pos_diff = 0, score_diff = 0;
        for(size_t k=0;k<NP;k++)
        {
            Rec r;
            const int s = fn(&positions[k], &r);
            st.add(full_score[k], s);
            if(s != full_score[k]) score_diff++;
            bool any = false;
            for(int i=0;i<r.n;i++)
            {
                pieces++;
                if(r.pc[i].to != full_to[full_off[k]+i]) { piece_diff++; any = true; }
                if(r.pc[i].term != full_term[full_off[k]+i]) term_diff++;
            }
            if(any) pos_diff++;
        }
        long long tg = 0, lv = 0, pc = 0;
        for(int t=0;t<6;t++) { tg += CNT.targets[t]; lv += CNT.levels[t]; pc += CNT.pieces[t]; }
        std::printf("    %-22s score differs %5.1f%%  mean|diff| %5.2f cp  corr %.4f  piece target differs %5.1f%% (term %5.1f%%)  positions w/ a differing target %5.1f%%  targets/call %5.1f  levels/piece %.2f\n",
                    name, 100.0 * score_diff / NP, st.mad(), st.corr(), 100.0 * piece_diff / pieces, 100.0 * term_diff / pieces, 100.0 * pos_diff / NP,
                    tg / (double)NP, lv / (double)pc);
        if(exact_expected && (score_diff || term_diff))
        {
            std::printf("      FAIL: expected identical scores and per-piece terms\n");
            failures++;
        }
    };
    std::printf("\n[3/4/5/8] variants vs the full term (same positions)\n");
    compare("full (reference)",      recd<2,0,UB_NONE,false,false>, true);
    compare("early stop, safe UB",   recd<2,0,UB_SAFE,false,false>, true);
    const long long stops_safe = CNT.stops;
    compare("early stop, emp. UB",   recd<2,0,UB_EMP,false,false>, false);
    const long long stops_emp = CNT.stops;
    std::printf("      stops taken: safe %lld, empirical %lld (of %zu pieces)\n", stops_safe, stops_emp, full_to.size());
    // empirical UB learned on the first half, tested on the second half
    {
        int saved[6]; for(int t=0;t<6;t++) saved[t] = EMP_UB[t];
        for(int t=0;t<6;t++) MAX_DB_SEEN[t] = 0;
        Rec r;
        for(size_t k=0;k<NP;k+=2) recd<2,0,UB_NONE,false,false>(&positions[k], &r);
        for(int t=0;t<6;t++) EMP_UB[t] = MAX_DB_SEEN[t];
        long long diff = 0, n = 0; double ad = 0;
        for(size_t k=1;k<NP;k+=2) { const int s = recd<2,0,UB_EMP,false,false>(&positions[k], &r); n++; if(s != full_score[k]) { diff++; ad += std::abs(s - full_score[k]); } }
        std::printf("      emp. UB from even positions, tested on odd: score differs on %lld/%lld (sum|diff| %.0f cp)\n", diff, n, ad);
        for(int t=0;t<6;t++) EMP_UB[t] = saved[t];
    }
    compare("cap N=1",               recd<2,1,UB_NONE,false,false>, false);
    compare("cap N=2",               recd<2,2,UB_NONE,false,false>, false);
    compare("cap N=3",               recd<2,3,UB_NONE,false,false>, false);
    compare("cheap db",              recd<2,0,UB_NONE,true,false>, false);
    compare("avoid lower attackers", recd<2,0,UB_NONE,false,true>, false);
    // [8] N distribution under AVOID
    {
        long long tn[6][5] = {}, tp[6] = {}, tnt[6] = {};
        for(size_t k=0;k<NP;k++)
        {
            Rec r;
            recd<2,0,UB_NONE,false,true>(&positions[k], &r);
            for(int i=0;i<r.n;i++) { const int t = r.pc[i].piece % 6; tp[t]++; if(r.pc[i].to < 0) tnt[t]++; else tn[t][std::min(r.pc[i].n,4)]++; }
        }
        std::printf("      avoid: type noTarget N=1/2/3/4+:");
        for(int t=1;t<6;t++)
        {
            const long long wt = tp[t] - tnt[t];
            std::printf("  %c %.0f%% %.0f/%.0f/%.0f/%.0f", TYPE_CH[t], 100.0*tnt[t]/tp[t], 100.0*tn[t][1]/wt, 100.0*tn[t][2]/wt, 100.0*tn[t][3]/wt, 100.0*tn[t][4]/wt);
        }
        std::printf("\n");
    }

    // how aggressive would an (inexact) bound have to be to matter?
    {
        int saved[6]; for(int t=0;t<6;t++) saved[t] = EMP_UB[t];
        const double f[4] = {0.75, 0.5, 0.35, 0.25};
        for(int i=0;i<4;i++)
        {
            for(int t=0;t<6;t++) EMP_UB[t] = (int)(saved[t] * f[i]);
            char name[64];
            std::snprintf(name, sizeof name, "emp. UB x %.2f", f[i]);
            compare(name, recd<2,0,UB_EMP,false,false>, false);
            double best = 1e30;
            for(int round=0; round<3; round++)
            {
                int s = 0;
                auto t0 = std::chrono::steady_clock::now();
                for(const BB& pos : positions) s += fast<2,0,UB_EMP,false,false>(&pos, WEIGHTS_OG);
                auto t1 = std::chrono::steady_clock::now();
                vsink0 += s;
                best = std::min(best, std::chrono::duration<double, std::micro>(t1 - t0).count() / NP);
            }
            std::printf("      -> %.3f us/call\n", best);
        }
        for(int t=0;t<6;t++) EMP_UB[t] = saved[t];
    }

    // ---- [6b] tempo: the same position with the other side to move
    {
        double sum = 0; std::vector<int> a;
        for(const BB& pos : positions)
        {
            BB f = pos;
            f.white_move = !f.white_move;
            const int x = plan_variant<2,0,UB_NONE,false,false,false>(&pos, WEIGHTS_OG, nullptr);
            const int y = plan_variant<2,0,UB_NONE,false,false,false>(&f, WEIGHTS_OG, nullptr);
            a.push_back(std::abs(x - y));
            sum += std::abs(x - y);
        }
        std::sort(a.begin(), a.end());
        std::printf("\n[6b] |plan(pos) - plan(pos, other side to move)|: mean %.1f cp  p95 %d  max %d\n", sum / NP, a[NP*95/100], a.back());
        // along the playouts: one ply (side to move flips) and two plies (it does not)
        double sd[3] = {}; long long nd[3] = {};
        for(size_t k=0;k<base.size();k++)
        for(int d=1; d<=2; d++)
        {
            if(k+d >= base.size() || line_of[k+d] != line_of[k]) continue;
            sd[d] += std::abs(plan_variant<2,0,UB_NONE,false,false,false>(&base[k+d], WEIGHTS_OG, nullptr) - plan_variant<2,0,UB_NONE,false,false,false>(&base[k], WEIGHTS_OG, nullptr));
            nd[d]++;
        }
        const double sc = sd[1], sg = sd[2]; const long long nc = nd[1], ng = nd[2];
        std::printf("     along the playouts: mean |plan| change over one ply %.1f cp (%lld), over two plies %.1f cp (%lld)\n", sc / nc, nc, sg / ng, ng);
    }

    // ---- [6] stability parent -> child
    {
        long long matched[2] = {}, same_map[2] = {}, same_best[2] = {}, same_term[2] = {}, child_pieces[2] = {}, npairs[2] = {};
        double sum_d[2] = {};
        long long same_map_ex[2] = {};
        for(size_t k=0;k<base.size();k++)
        {
            const BB& par = base[k];
            int n = std::get<0>(all_moves(&par, wfh));
            if(n == 0) continue;
            const BB child = wfh[rnd() % n];
            const int cap = pieces_of(child, !par.white_move) < pieces_of(par, !par.white_move) ? 1 : 0;
            static Rec rp, rc;
            const int sp = recd<2,0,UB_NONE,false,false>(&par, &rp);
            const int sc = recd<2,0,UB_NONE,false,false>(&child, &rc);
            npairs[cap]++;
            sum_d[cap] += std::abs(sc - sp);
            for(int i=0;i<rc.n;i++)
            {
                child_pieces[cap]++;
                const Piece_Rec& c = rc.pc[i];
                for(int j=0;j<rp.n;j++)
                {
                    const Piece_Rec& p = rp.pc[j];
                    if(p.piece != c.piece || p.from != c.from) continue;
                    matched[cap]++;
                    bool map = p.reached == c.reached;
                    for(int s=0; map && s<64; s++) map = p.db_at[s] == c.db_at[s];
                    if(map) same_map[cap]++;
                    if(p.to == c.to && p.db == c.db) same_best[cap]++;
                    if(p.term == c.term) same_term[cap]++;
                    break;
                }
            }
        }
        std::printf("\n[6] parent -> random legal child (%lld quiet, %lld capture pairs)\n", npairs[0], npairs[1]);
        for(int c=0;c<2;c++)
        std::printf("    %-7s mean|plan(child)-plan(parent)| %5.1f cp; of the child's pieces %.1f%% stand where they stood; of those: reached set + all db identical %5.1f%%, best (to,db) unchanged %5.1f%%, term unchanged %5.1f%%\n",
                    c ? "capture" : "quiet", sum_d[c] / npairs[c], 100.0 * matched[c] / child_pieces[c],
                    100.0 * same_map[c] / matched[c], 100.0 * same_best[c] / matched[c], 100.0 * same_term[c] / matched[c]);
    }

    // ---- timing: rounds of every variant, best round kept
    std::vector<Timed> timed = {
        {"basic_eval (lib, with plan)", basic_eval_lib, 1e30},
        {"basic_eval without plan",     basic_eval_noplan, 1e30},
        {"plan_eval (lib)",             plan_eval_lib, 1e30},
        {"plan copy: setup only",       fast<0,0,UB_NONE,false,false>, 1e30},
        {"plan copy: setup+BFS",        fast<1,0,UB_NONE,false,false>, 1e30},
        {"plan copy: full",             fast<2,0,UB_NONE,false,false>, 1e30},
        {"early stop, safe UB",         fast<2,0,UB_SAFE,false,false>, 1e30},
        {"  (safe UB setup only)",      fast<0,0,UB_SAFE,false,false>, 1e30},
        {"early stop, emp. UB",         fast<2,0,UB_EMP,false,false>, 1e30},
        {"cap N=1",                     fast<2,1,UB_NONE,false,false>, 1e30},
        {"cap N=2",                     fast<2,2,UB_NONE,false,false>, 1e30},
        {"cap N=3",                     fast<2,3,UB_NONE,false,false>, 1e30},
        {"cheap db",                    fast<2,0,UB_NONE,true,false>, 1e30},
        {"cheap db + cap N=2",          fast<2,2,UB_NONE,true,false>, 1e30},
        {"avoid lower attackers",       fast<2,0,UB_NONE,false,true>, 1e30},
    };
    volatile int vsink = 0;
    const int ROUNDS = 7, REPS = 3;
    for(int round=0; round<ROUNDS; round++)
    for(Timed& t : timed)
    {
        int s = 0;
        auto t0 = std::chrono::steady_clock::now();
        for(int rep=0; rep<REPS; rep++)
        for(const BB& pos : positions)
        s += t.f(&pos, WEIGHTS_OG);
        auto t1 = std::chrono::steady_clock::now();
        vsink += s;
        t.best_us = std::min(t.best_us, std::chrono::duration<double, std::micro>(t1 - t0).count() / (REPS * (double)NP));
    }
    std::printf("\n[1/timing] us/call, best of %d rounds x %d reps x %zu positions\n", ROUNDS, REPS, NP);
    for(const Timed& t : timed)
    std::printf("    %-30s %7.3f\n", t.name, t.best_us);

    // basic_eval with a cold lazy cache (attacks not yet computed), copy+reset cost subtracted
    {
        double hot = 1e30, cold = 1e30, copy = 1e30, coldn = 1e30;
        for(int round=0; round<ROUNDS; round++)
        {
            for(int f=0; f<3; f++)
            {
                int s = 0;
                auto t0 = std::chrono::steady_clock::now();
                for(int rep=0; rep<REPS; rep++)
                for(const BB& pos : positions)
                {
                    BB tmp = pos;
                    tmp.lazy.reset();
                    if(f == 0) s += basic_eval_lib(&tmp, WEIGHTS_OG);
                    else if(f == 1) s += basic_eval_noplan(&tmp, WEIGHTS_OG);
                    else s += tmp.Board[0] & 1;
                }
                auto t1 = std::chrono::steady_clock::now();
                vsink += s;
                const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / (REPS * (double)NP);
                double* o = f == 0 ? &cold : f == 1 ? &coldn : &copy;
                *o = std::min(*o, us);
            }
        }
        std::printf("    cold lazy cache (copy+reset %.3f subtracted): basic_eval %.3f, without plan %.3f\n", copy, cold - copy, coldn - copy);
    }

    // the same split on the capture children only
    std::vector<BB> capset(caps.begin(), caps.end());
    double cb = 1e30, cn = 1e30, cp = 1e30;
    for(int round=0; round<ROUNDS; round++)
    {
        Eval_Fn fs[3] = {basic_eval_lib, basic_eval_noplan, fast<2,0,UB_NONE,false,false>};
        double* out[3] = {&cb, &cn, &cp};
        for(int f=0; f<3; f++)
        {
            int s = 0;
            auto t0 = std::chrono::steady_clock::now();
            for(int rep=0; rep<REPS; rep++) for(const BB& pos : capset) s += fs[f](&pos, WEIGHTS_OG);
            auto t1 = std::chrono::steady_clock::now();
            vsink += s;
            *out[f] = std::min(*out[f], std::chrono::duration<double, std::micro>(t1 - t0).count() / (REPS * (double)capset.size()));
        }
    }
    std::printf("    capture children only: basic_eval %.3f, without plan %.3f, plan copy %.3f\n", cb, cn, cp);

    delete[] wfh;
    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "all checks passed", failures);
    return failures ? 1 : 0;
}
