// OWNERSHIP=Claude
// tools/tune: fits a weight set to game results (issue #87, docs/TUNING_PLAN.md "Method").
//
//   ./tools/tune [data=data/tune] [from=weights/w1.txt] [out=weights/w<from+1>.txt]
//                [jobs=6] [lambda=1e-9] [k=0] [tag=ccrl4040-3000-balanced]
//                [method=gn] [iters=20] [mu=1e-3] [tol=1e-7] [check=0]   (Levenberg-Marquardt)
//                [method=adam] [epochs=1500] [lr=2]                      (Adam)
//                [gauge=pin|pin104|free] [se=1] [compare=<set>] [spectrum=<file>]
//
// Reads #86's train/valid/test.tsv (fen and result columns only) and minimises the
// mean of (sigma(eval/K) - r)^2 over train, eval = basic_eval() (static, net off) in
// centipawns from white's view, r in {1, 0.5, 0}, plus lambda * sum (w - w_from)^2.
// K is fitted once, on the `from` set's real basic_eval() (k=0), or given, and then
// held fixed.
//
// The fit runs on a model of basic_eval() that is exact up to its integer
// truncations: per position the pieces on the piece-square tables (with the game
// phase, game_phase()), and the coefficient of every other tuned weight. Those
// coefficients are taken from the real term functions (material_eval(),
// positional_eval(), passed_pawn_eval(), pawn_shape_eval(), placement_eval(),
// threat_eval(), piece_activity_eval(), tempo_eval(), king_safety_detail()) with
// one weight set to a probe value and the rest of that
// term at 0, so the model cannot drift from the eval. Every term is linear in its
// weights except king danger, which the model keeps in closed form, so the gradient
// is exact: max(0, units)^2 / ks_danger_div, units linear in the ks_* weights.
// Held fixed: the pawn's opening value (the centipawn), the king values (never
// read by basic_eval()), ks_min_attackers (a gate),
// ks_danger_div (its scale is the units' scale), ks_storm_blocked_div and
// ks_full_piece_material (divisors), and the weights no train position touches. A
// constant moved between a piece's value and its square table or mobility table leaves
// the eval alone: gauge=pin (default, #104, "the gauge" below) fixes those directions,
// so a piece value is the piece's whole value; with gauge=free only lambda holds them.
// lambda also holds back the rarely seen weights:
// lambda=0 fits valid best, but rare table squares and ks_storm entries run out to
// +-400 cp; 1e-9 keeps the tables within +-150 for most of the gain
// (docs/measurements/tune_w2_2026-10-04.md). A fit from its own result is therefore a
// new fit, not more of the old one: the pull moves to the new start, and the loss falls
// again (w7 = w6 refitted twice, #97). One run does reach its minimum (#104).
//
// gauge=pin also holds the three directions the means leave exact or near-exact (#106,
// regauge_degenerate(), docs/measurements/tune_pin3_2026-10-08.md): the endgame passed bonus
// on the 7th against the pawn table, the knight's defend count against its mobility and
// tables, and the ks_shelter constant against material (exact since #107). With them
// lambda=0 has finite SEs for every piece value; gauge=pin104 is the gauge without them.
//
// Two methods (#104), both keeping the step with the lowest valid loss of the model.
// Both reach the same minimum; gn in a sixth of the time (docs/measurements/tune_gn_2026-10-08.md).
// - adam: full-batch Adam, the learning rate falling from lr to lr/100 (cosine).
// - gn (default): Levenberg-Marquardt on the same objective F = mean (q_i - r_i)^2 + lambda |w - w_from|^2,
//   q_i = sigma(e_i/K). Per position J_i = q_i (1 - q_i)/K de_i/dw (model_eval()'s gradient
//   path), so F's Gauss-Newton Hessian is 2 A and its gradient 2 g with A = J'J/n + lambda I,
//   g = J'(q - r)/n + lambda (w - w_from), over the tuned weights only. A step solves
//   (A + mu diag A) d = -g by Cholesky; it is kept if F falls (mu / 5), else mu * 5 and
//   again. Stops after iters steps or when one gains less than tol * F. In the pinned
//   gauge the solver drops one square per table (its anchor moves by minus the others'
//   steps); Adam takes each step's mean off every pinned table. lambda=0 works (LM's
//   damping keeps every step defined). check=1 prints g
//   against Adam's gradient once (2 g = Adam's to rounding).
// The weights are rounded, and the report gives train/valid/test loss of both sets by
// the real basic_eval(). It is printed and written as '#' lines at the end of the new set.
//
// Standard errors (se=1), at the kept point, both methods: Cov(w) ~ sigma^2/n A^-1
// (in the solver's coordinates, an anchor's variance from the rest of its table),
// sigma^2 the mean squared residual on train. That treats the residuals as independent
// with one variance (they are not: positions of one game share a result, and a result's
// variance depends on q), so the SEs are a lower bound of the sampling noise. SE is the
// plain sqrt(Cov_jj). In the pinned gauge SE* = SE. With gauge=free, SE* is gauge-free: a table or mobility entry against the mean of its
// table, a piece value plus the means of its square table and mobility table (a constant
// moved between them leaves the eval alone, so only lambda holds their split, and the
// plain SEs of those weights are mostly lambda's). lambda is a prior of sd
// sqrt(sigma^2/(n lambda)) around `from` (6.8 cp on tune2 at 1e-9), so it caps every SE.
// A weight is flagged "lambda" when its SE* shrinks by more than 1.5 with 10 lambda: the
// prior gives it more than about 1/7 of its precision. "no-data": no position touches it.
// If A is singular (lambda=0), the SEs take the smallest ridge (from 1e-15 * the largest
// diagonal, x10) that lets Cholesky through. The report names the 40 weakest directions
// either way (#107: A's eigenvalues, each with the SE sqrt(sigma^2/(n eigenvalue)) of the
// weights' combination along it); spectrum=<file> writes all of them. The SEs are written as
// '# se' lines after the report, with how often each weight fires in train.
// compare=<set> (rewritten into the gauge too) counts the differences between that set
// and the new one beyond 2 SE*.
// Memory: one n_tuned^2 double matrix per thread (~9 MB each) and three more.
//
// Memory: about 230 bytes per train or valid position (most of it the feature
// words), 30 per test position. The words go into fixed blocks that never move,
// rather than one array that would hold them twice while it grows, and the FENs
// are dropped once the features are made: the real eval's losses at the end read
// the files again, after the features are freed.
#include "../lib/uci.hpp"
#include "../lib/king_safety.hpp"
#include "../lib/weight_set.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef ENGINE_COMMIT
#define ENGINE_COMMIT "unknown"
#endif

struct Options
{
    std::string data = "data/tune";
    std::string from = "weights/w1.txt";
    std::string out;                    // default weights/w<version>.txt
    std::string tag = "ccrl4040-3000-balanced";
    int jobs = 6;
    int epochs = 1500;
    double lr = 2;
    double lambda = 1e-9;
    double k = 0;                       // 0: fit it
    std::string method = "gn";          // gn (Levenberg-Marquardt) or adam
    int iters = 20;                     // gn: at most this many steps
    double mu = 1e-3;                   // gn: initial damping
    double tol = 1e-7;                  // gn: stop when a step gains less than tol * loss
    bool se = true;                     // standard errors at the end
    bool check = false;                 // gn: its gradient against Adam's, once
    std::string compare;                // a set whose differences to the new one are judged by the SEs
    bool pin = true;                    // gauge=pin: table means and mobility constants in the piece values
    bool pin3 = true;                   // gauge=pin (not pin104): and the three degenerate directions (#106)
    std::string spectrum;               // se=1: every eigenvalue of A and its direction, to this file
};

// ---- the parameters: every number of a weight set, flat, in weight_fields() order

static const int MAX_PARAMS = 4096;
static const int WEAK_DIRECTIONS = 40;  // the report names this many of the weakest directions
static const uint32_t PARAM_MASK = MAX_PARAMS-1;   // a linear feature word's parameter bits
static int n_params = 0;
static std::string param_name[MAX_PARAMS];
static int param_offset[MAX_PARAMS];   // byte offset of the int inside a WEIGHTS
static bool param_tuned[MAX_PARAMS];
static double param_den[MAX_PARAMS];   // a linear feature's coefficient is num / den
static int flat_of_offset[sizeof(WEIGHTS)/sizeof(int)+1];

static int flat(const WEIGHTS& W, const int* p)
{
    return flat_of_offset[((const char*)p - (const char*)&W)/sizeof(int)];
}
static int& at(WEIGHTS& W, int j) { return *(int*)((char*)&W + param_offset[j]); }

static void build_params(const WEIGHTS& from)
{
    WEIGHTS W = from;
    Weight_Field fields[MAX_WEIGHT_FIELDS];
    const int nf = weight_fields(W, fields);
    for(size_t i=0;i<sizeof(flat_of_offset)/sizeof(int);i++) flat_of_offset[i] = -1;
    for(int f=0;f<nf;f++)
    for(int k=0;k<fields[f].count;k++)
    {
        const int j = n_params++;
        param_name[j] = fields[f].key + (fields[f].count>1 ? "[" + std::to_string(k) + "]" : "");
        param_offset[j] = (char*)&fields[f].values[k] - (char*)&W;
        flat_of_offset[param_offset[j]/sizeof(int)] = j;
        param_tuned[j] = true;
        param_den[j] = 1;
    }
    for(const int* p : {&W.piece_value[0], &W.piece_value[5], &W.piece_value_endgame[5], &W.ks_min_attackers, &W.ks_danger_div, &W.ks_storm_blocked_div, &W.ks_full_piece_material})
    param_tuned[flat(W, p)] = false;
}

// ---- the gauge (#104)

// A constant added to every square of a piece's table, or to every entry of one of its
// mobility tables, and taken off its value, leaves the eval alone (each piece stands on one
// square and has one mobility count; both blend by the same phase). gauge=pin fixes those
// directions: every table's mean over the squares the piece can stand on is 0 (pawns: ranks
// 2-7), and every mobility table is 0 at a typical count, so the piece value is the whole
// value of a piece with typical mobility on an average square. The king's constant cancels
// (one king a side) and is dropped. The pawn's opening value stays the unit, held: it takes
// its table's mean (w7: 140 -> 137) and is then fixed as before. The rewrite moves integers
// between material_eval(), piecetable() and the mobility sum, which basic_eval() each divide
// by PHASE_MAX and truncate apart, so the real eval moves by a few cp on some positions; the
// model (no truncation) does not move at all.
static const int MOB_PIN[6] = {-1, 5, 6, 7, 9, -1};     // pawn rook knight bishop queen king: the most frequent counts on tune2
static const int MOB_SIZE[6] = {0, 15, 9, 14, 28, 0};

static int* mobility_table(WEIGHTS& W, int piece, int ph)
{
    switch(piece)
    {
        case 1: return ph ? W.mobility_rook_endgame : W.mobility_rook_opening;
        case 2: return ph ? W.mobility_knight_endgame : W.mobility_knight_opening;
        case 3: return ph ? W.mobility_bishop_endgame : W.mobility_bishop_opening;
        case 4: return ph ? W.mobility_queen_endgame : W.mobility_queen_opening;
    }
    return nullptr;
}
static bool on_table(int piece, int sq) { return piece!=0 || (sq>=8 && sq<56); }

// #106: the three directions the data cannot fix even with the means pinned. Each is rewritten
// out of one weight, which is then held (param_tuned false, set in main()):
//  - activity_knight_defend D = 0. A knight's mobility count is m = (squares it attacks) -
//    (own pieces it defends) and the attacked squares depend only on the square, so D per
//    defended piece = D*A(sq) on the knight tables - D*m on both mobility tables (the blend
//    weights sum to PHASE_MAX, so no truncation moves apart).
//  - passed_pawn_value[6] (endgame) = 0. Every pawn on the 7th is passed, so the bonus is a
//    constant on that rank of the pawn endgame table (same phase blend). The opening bonus
//    has the same identity, but the held opening pawn value (the unit) already breaks it
//    (SE 9.9 cp at lambda 0 with only the #104 pins): pinning it too would cost a real dof.
//  - ks_shelter[2] (a king behind its home pawn) ~ 0. The shelter penalty is scaled by the
//    enemy's phase material M = N+B+2R+4Q over ks_full_piece_material = 12 (the whole army;
//    no cap since #107, a promotion takes it past 1), each of 3 files carrying one entry, so a constant d on
//    the entries is 3d*M/12 = d*M/4 per king: d/4 on a knight and a bishop, d/2 on a rook, d
//    on a queen, in both phases. d is ks_shelter[2] rounded to a multiple of 4, so those are
//    integers. Exact but for the penalty's truncation.
// The rewrite is applied before the means, so both constraints hold after it.
static bool PIN_DEGENERATE = true;
static const int GAUGE_KS_SHELTER = 2;

static void regauge_degenerate(WEIGHTS& W)
{
    const int D = W.activity_knight_defend;
    for(int ph=0;ph<2;ph++)
    {
        int* tab = ph ? W.piece_table_value_endgame[2] : W.piece_table_value_opening[2];
        for(int sq=0;sq<64;sq++) tab[sq] += D*__builtin_popcountll(Kn_template[sq]);
        int* mob = mobility_table(W, 2, ph);
        for(int k=0;k<MOB_SIZE[2];k++) mob[k] -= D*k;
    }
    for(int sq=48;sq<56;sq++) W.piece_table_value_endgame[0][sq] += W.passed_pawn_value[6];
    W.passed_pawn_value[6] = 0;
    W.activity_knight_defend = 0;
    const int d = 4*(int)std::lround(W.ks_shelter[GAUGE_KS_SHELTER]/4.0);
    for(int k=0;k<8;k++) W.ks_shelter[k] -= d;
    for(int ph=0;ph<2;ph++)
    {
        int* value = ph ? W.piece_value_endgame : W.piece_value;
        value[1] += d/2; value[2] += d/4; value[3] += d/4; value[4] += d;
    }
}

// W rewritten into the pinned gauge, the eval unchanged up to rounding the table means.
static void regauge(WEIGHTS& W)
{
    if(PIN_DEGENERATE) regauge_degenerate(W);
    for(int piece=0;piece<6;piece++)
    for(int ph=0;ph<2;ph++)
    {
        int* tab = ph ? W.piece_table_value_endgame[piece] : W.piece_table_value_opening[piece];
        int* value = ph ? W.piece_value_endgame : W.piece_value;
        long sum = 0; int n = 0;
        for(int sq=0;sq<64;sq++) if(on_table(piece, sq)) { sum += tab[sq]; n++; }
        const int c = (int)std::lround((double)sum/n);
        for(int sq=0;sq<64;sq++) if(on_table(piece, sq)) tab[sq] -= c;
        if(piece<5) value[piece] += c;
        if(int* mob = mobility_table(W, piece, ph))
        {
            const int m = mob[MOB_PIN[piece]];
            for(int k=0;k<MOB_SIZE[piece];k++) mob[k] -= m;
            value[piece] += m;
        }
    }
}

// make_features() compares basic_eval() with W_FROM against W_CHECK (the set as read)
static const WEIGHTS* W_CHECK = nullptr;
static int check_worst = 0;
static size_t check_n = 0, check_differ = 0, check_sum = 0, check_big = 0;

// ---- one position as the model sees it

// Feature words. The table pieces first, two to a word (16 bits each): bits 0-8
// piece*64 + square (white's view), bit 9 set for black; they share the position's
// game phase (0..PHASE_MAX, the endgame weight is PHASE_MAX - phase). Then the
// linear and danger features, one to a word: bits 0-11 the parameter, bits 16-31
// the numerator (int16).
struct Pos
{
    const uint32_t* w;          // first feature word
    uint8_t n_tab, n_lin, n_ks[2];   // n_ks[1]: white king's danger units (gated in)
    uint8_t r2;                 // 2 * result
    uint8_t phase;              // game_phase()
};

static const size_t BLOCK_WORDS = 1<<20;   // 4 MB

struct Split
{
    std::vector<Pos> pos;
    std::vector<std::unique_ptr<uint32_t[]>> blocks;   // the words; a position's never straddle two
    size_t n_words = 0;
    std::vector<int> base;      // basic_eval() with the from set
    std::string blob;           // every FEN, back to back
    std::vector<uint32_t> fen_at{0};   // FEN i is blob[fen_at[i], fen_at[i+1])
    size_t count() const { return fen_at.size()-1; }
    std::string fen(size_t i) const { return blob.substr(fen_at[i], fen_at[i+1]-fen_at[i]); }
};

static WEIGHTS W_FROM;
static int TO_BASE, TE_BASE;            // flat index of the tables' [0][0]

struct Probe_Group { std::vector<int> params; int value; };
static Probe_Group G_MATERIAL, G_POSITIONAL, G_PASSED, G_SHAPE, G_PLACEMENT, G_THREAT, G_ACTIVITY, G_TEMPO, G_KS_ATTACK, G_KS_SHELTER;

static void build_groups()
{
    WEIGHTS& W = W_FROM;
    auto add = [&](Probe_Group& g, const int* p, int n) { for(int i=0;i<n;i++) g.params.push_back(flat(W, p+i)); };
    add(G_MATERIAL, W.piece_value, 5);  // the kings cancel
    add(G_MATERIAL, W.piece_value_endgame, 5);
    G_MATERIAL.value = PHASE_MAX;       // material_eval(): /PHASE_MAX, exact
    add(G_POSITIONAL, &W.punishment_for_double_pawn, 1);
    add(G_POSITIONAL, &W.punishment_for_trippled_pawn, 1);
    add(G_POSITIONAL, &W.punishment_for_isolated_pawn, 1);
    G_POSITIONAL.value = 1;
    add(G_PASSED, W.passed_pawn_value_opening, 8);
    add(G_PASSED, W.passed_pawn_value, 8);
    add(G_PASSED, W.passed_free_path, 8);   // #90's modifiers: v*endgame/PHASE_MAX
    add(G_PASSED, W.passed_king_enemy, 8);
    add(G_PASSED, W.passed_king_own, 8);
    add(G_PASSED, W.passed_supported, 8);
    add(G_PASSED, &W.passed_rook_behind, 1);
    add(G_PASSED, &W.passed_unstoppable, 1);   // a max over the passers: linear while >= 0
    G_PASSED.value = PHASE_MAX;         // every part divides by PHASE_MAX: exact
    add(G_SHAPE, &W.pawn_backward_opening, 1);
    add(G_SHAPE, &W.pawn_backward_endgame, 1);
    add(G_SHAPE, W.pawn_phalanx_opening, 8);
    add(G_SHAPE, W.pawn_phalanx_endgame, 8);
    G_SHAPE.value = PHASE_MAX;          // pawn_shape_eval(): /PHASE_MAX, exact
    for(const int* p : {&W.bishop_pair_opening, &W.bishop_pair_endgame, &W.rook_open_file_opening, &W.rook_open_file_endgame,
                        &W.rook_semi_open_file_opening, &W.rook_semi_open_file_endgame, &W.rook_seventh_opening, &W.rook_seventh_endgame,
                        &W.outpost_knight_opening, &W.outpost_knight_endgame, &W.outpost_bishop_opening, &W.outpost_bishop_endgame})
    add(G_PLACEMENT, p, 1);
    G_PLACEMENT.value = PHASE_MAX;      // placement_eval(): /PHASE_MAX, exact
    add(G_THREAT, W.threat_by_pawn, 5);
    add(G_THREAT, W.threat_by_minor, 5);
    add(G_THREAT, W.threat_by_rook, 5);
    add(G_THREAT, &W.threat_hanging, 1);
    G_THREAT.value = 1;                 // counts
    for(const int* p : {&W.activity_pawn_attack, &W.activity_pawn_defend, &W.activity_pawn_blocked,
                        &W.activity_pawn_push_attack, &W.activity_pawn_push_defend,
                        &W.activity_bishop_defend, &W.activity_bishop_attack,
                        &W.activity_rook_defend, &W.activity_rook_attack,
                        &W.activity_queen_defend, &W.activity_queen_attack,
                        &W.activity_knight_defend, &W.activity_knight_attack,
                        &W.activity_king_defend, &W.activity_king_attack})
    add(G_ACTIVITY, p, 1);
    add(G_ACTIVITY, W.mobility_knight_opening, 9);
    add(G_ACTIVITY, W.mobility_knight_endgame, 9);
    add(G_ACTIVITY, W.mobility_bishop_opening, 14);
    add(G_ACTIVITY, W.mobility_bishop_endgame, 14);
    add(G_ACTIVITY, W.mobility_rook_opening, 15);
    add(G_ACTIVITY, W.mobility_rook_endgame, 15);
    add(G_ACTIVITY, W.mobility_queen_opening, 28);
    add(G_ACTIVITY, W.mobility_queen_endgame, 28);
    G_ACTIVITY.value = PHASE_MAX;       // the mobility sum divides by PHASE_MAX: exact
    add(G_TEMPO, &W.tempo_opening, 1);
    add(G_TEMPO, &W.tempo_endgame, 1);
    G_TEMPO.value = PHASE_MAX;          // tempo_eval(): /PHASE_MAX, exact
    add(G_KS_ATTACK, W.ks_attacker_weight+1, 4);   // [0] and [5] are never read
    add(G_KS_ATTACK, &W.ks_hit, 1);
    add(G_KS_ATTACK, &W.ks_weak_ring_square, 1);
    add(G_KS_ATTACK, W.ks_safe_check+1, 4);
    add(G_KS_ATTACK, &W.ks_unsafe_check, 1);
    add(G_KS_ATTACK, &W.ks_no_queen, 1);
    G_KS_ATTACK.value = 1;              // units are integers
    add(G_KS_SHELTER, W.ks_shelter, 8);
    add(G_KS_SHELTER, &W.ks_open_file, 1);
    add(G_KS_SHELTER, W.ks_storm, 8);
    G_KS_SHELTER.value = W.ks_storm_blocked_div*W.ks_full_piece_material;   // both divisions exact

    for(int j : G_MATERIAL.params) param_den[j] = G_MATERIAL.value;
    for(int j : G_POSITIONAL.params) param_den[j] = 1;
    for(int j : G_PASSED.params) param_den[j] = G_PASSED.value;
    for(int j : G_SHAPE.params) param_den[j] = G_SHAPE.value;
    for(int j : G_PLACEMENT.params) param_den[j] = G_PLACEMENT.value;
    for(int j : G_THREAT.params) param_den[j] = G_THREAT.value;
    for(int j : G_ACTIVITY.params) param_den[j] = G_ACTIVITY.value;
    for(int j : G_TEMPO.params) param_den[j] = G_TEMPO.value;
    for(int j : G_KS_SHELTER.params) param_den[j] = G_KS_SHELTER.value;
}

static uint32_t lin_word(int j, int num)
{
    if(num<INT16_MIN || num>INT16_MAX) { std::fprintf(stderr, "coefficient %d of %s out of range\n", num, param_name[j].c_str()); std::exit(1); }
    return (uint32_t)j | (uint32_t)(uint16_t)(int16_t)num << 16;
}

// The features of one position, appended to `words`.
static Pos make_pos(const BB& b, double result, std::vector<uint32_t>& words)
{
    Pos p{};
    p.r2 = (uint8_t)std::lround(2*result);
    // piecetable(): both sides by the one game phase
    p.phase = game_phase(&b);
    for(int piece=0;piece<12;piece++)
    {
        uint64_t bb = b.Board[piece];
        while(bb)
        {
            const int sq = find_and_delete_trailling_1(bb);
            const bool black = piece>=6;
            const uint32_t entry = (uint32_t)((piece%6)*64 + (black ? sq^56 : sq)) | (black ? 1u<<9 : 0);
            if(p.n_tab%2==0) words.push_back(entry);
            else words.back() |= entry << 16;
            p.n_tab++;
        }
    }
    // linear terms: f(probe)/value per weight, the rest of the term at 0
    int acc[MAX_PARAMS];
    std::vector<int> touched;
    auto put = [&](int j, int num) { if(!num) return; if(std::find(touched.begin(), touched.end(), j)==touched.end()) { touched.push_back(j); acc[j] = 0; } acc[j] += num; };
    WEIGHTS P = W_FROM;
    auto probe = [&](const Probe_Group& g, auto&& term)
    {
        for(int j : g.params) at(P, j) = 0;
        for(int j : g.params)
        {
            at(P, j) = g.value;
            put(j, term());
            at(P, j) = 0;
        }
    };
    probe(G_MATERIAL, [&]{ return material_eval(&b, P); });
    probe(G_POSITIONAL, [&]{ return positional_eval(&b, P); });
    probe(G_PASSED, [&]{ return passed_pawn_eval(&b, P); });
    probe(G_SHAPE, [&]{ return pawn_shape_eval(&b, P); });
    probe(G_PLACEMENT, [&]{ return placement_eval(&b, P); });
    probe(G_THREAT, [&]{ return threat_eval(&b, P); });
    probe(G_ACTIVITY, [&]{ return piece_activity_eval(&b, P); });
    probe(G_TEMPO, [&]{ return tempo_eval(&b, P); });
    // king safety: white = -(attack + shelter), black likewise, eval = white - black
    std::vector<uint32_t> ks[2];
    for(int j : G_KS_ATTACK.params) at(P, j) = 0;
    for(int j : G_KS_SHELTER.params) at(P, j) = 0;
    for(int side=0;side<2;side++)
    {
        const int sign = side ? -1 : 1;
        const bool gated_in = king_safety_detail(&b, side, P).attackers >= W_FROM.ks_min_attackers;
        const size_t n = std::max(G_KS_ATTACK.params.size(), G_KS_SHELTER.params.size());
        for(size_t k=0;k<n;k++)
        {
            const int a = k<G_KS_ATTACK.params.size() ? G_KS_ATTACK.params[k] : -1;
            const int s = k<G_KS_SHELTER.params.size() ? G_KS_SHELTER.params[k] : -1;
            if(a>=0) at(P, a) = G_KS_ATTACK.value;
            if(s>=0) at(P, s) = G_KS_SHELTER.value;
            const King_Safety_Detail d = king_safety_detail(&b, side, P);
            if(a>=0 && gated_in && d.danger_units) ks[side].push_back(lin_word(a, d.danger_units));
            if(s>=0) put(s, sign*d.shelter_penalty);
            if(a>=0) at(P, a) = 0;
            if(s>=0) at(P, s) = 0;
        }
        if(!gated_in) ks[side].clear();
    }
    int n_lin = 0;
    for(int j : touched)
    if(acc[j]) { words.push_back(lin_word(j, acc[j])); n_lin++; }
    if(n_lin>255 || ks[0].size()>255 || ks[1].size()>255) { BB c = b; std::fprintf(stderr, "more than 255 features of one kind in %s\n", c.get_FEN().c_str()); std::exit(1); }
    p.n_lin = n_lin;
    for(int side=1;side>=0;side--)
    {
        words.insert(words.end(), ks[side].begin(), ks[side].end());
        p.n_ks[side] = ks[side].size();
    }
    return p;
}

// ---- the model

// The model's eval of p at theta; with grad, adds g * d eval / d theta to grad.
static inline double model_eval(const Pos& p, const double* th, double* grad, double g)
{
    const uint32_t* w = p.w;
    const int n_tab_words = (p.n_tab+1)/2;
    double e = 0;
    // tables
    double opening = 0, endgame = 0;
    for(int i=0;i<p.n_tab;i++)
    {
        const uint32_t entry = w[i>>1] >> (16*(i&1));
        const int idx = entry & 511;
        if(entry & 512) { opening -= th[TO_BASE+idx]; endgame -= th[TE_BASE+idx]; }
        else            { opening += th[TO_BASE+idx]; endgame += th[TE_BASE+idx]; }
    }
    e += (opening*p.phase + endgame*(PHASE_MAX-p.phase))/PHASE_MAX;
    w += n_tab_words;
    // linear
    const uint32_t* lin = w;
    for(int i=0;i<p.n_lin;i++, w++)
    e += th[*w & PARAM_MASK]*(int16_t)(*w >> 16)/param_den[*w & PARAM_MASK];
    // king danger: white's lowers the eval, black's raises it
    const uint32_t* ks = w;
    double units[2] = {0, 0};
    for(int side=1;side>=0;side--)
    for(int i=0;i<p.n_ks[side];i++, w++)
    units[side] += th[*w & PARAM_MASK]*(int16_t)(*w >> 16);
    const double div = th[flat(W_FROM, &W_FROM.ks_danger_div)];
    for(int side=0;side<2;side++)
    if(units[side]>0)
    e += (side ? -1 : 1)*units[side]*units[side]/div;

    if(grad)
    {
        const double go = g*p.phase/PHASE_MAX, ge = g*(PHASE_MAX-p.phase)/PHASE_MAX;
        for(int i=0;i<p.n_tab;i++)
        {
            const uint32_t entry = p.w[i>>1] >> (16*(i&1));
            const int idx = entry & 511;
            if(entry & 512) { grad[TO_BASE+idx] -= go; grad[TE_BASE+idx] -= ge; }
            else            { grad[TO_BASE+idx] += go; grad[TE_BASE+idx] += ge; }
        }
        for(int i=0;i<p.n_lin;i++)
        grad[lin[i] & PARAM_MASK] += g*(int16_t)(lin[i] >> 16)/param_den[lin[i] & PARAM_MASK];
        w = ks;
        for(int side=1;side>=0;side--)
        for(int i=0;i<p.n_ks[side];i++, w++)
        if(units[side]>0)
        grad[*w & PARAM_MASK] += g*(side ? -1 : 1)*2*units[side]/div*(int16_t)(*w >> 16);
    }
    return e;
}

static inline double sigmoid(double x) { return 1/(1+std::exp(-x)); }

// Mean (sigma(e/K) - r)^2 of the model over s; with grad, the mean gradient.
static double model_loss(const Split& s, const double* th, double K, int jobs, double* grad)
{
    const size_t n = s.pos.size();
    std::vector<double> part(jobs, 0);
    std::vector<std::vector<double>> g(grad ? jobs : 0, std::vector<double>(n_params, 0));
    std::vector<std::thread> pool;
    for(int t=0;t<jobs;t++)
    pool.emplace_back([&, t]
    {
        double sum = 0;
        double* gt = grad ? g[t].data() : nullptr;
        for(size_t i=n*t/jobs;i<n*(t+1)/jobs;i++)
        {
            const Pos& p = s.pos[i];
            const double r = p.r2*0.5;
            const double e = model_eval(p, th, nullptr, 0);
            const double q = sigmoid(e/K);
            sum += (q-r)*(q-r);
            if(gt) model_eval(p, th, gt, 2*(q-r)*q*(1-q)/K);
        }
        part[t] = sum;
    });
    for(auto& th_ : pool) th_.join();
    double sum = 0;
    for(double x : part) sum += x;
    if(grad)
    for(int j=0;j<n_params;j++)
    {
        double x = 0;
        for(int t=0;t<jobs;t++) x += g[t][j];
        grad[j] = x/n;
    }
    return sum/n;
}

// ---- Gauss-Newton: the normal equations over the tuned weights

static int n_tuned = 0;
static int tix[MAX_PARAMS];             // flat -> tuned index, -1 if held fixed
static int jof[MAX_PARAMS];             // tuned index -> flat

// A = J'J/n + lambda I (upper triangle, row-major, n_tuned^2) and g = J'r/n + lambda (th - th0),
// J_i = sigma'(e_i/K)/K de_i/dth, r_i = sigma(e_i/K) - result: half the objective's gradient
// and Gauss-Newton Hessian. Returns the mean squared residual.
static double normal_eqs(const Split& s, const double* th, const double* th0, double K, double lambda, int jobs,
                         std::vector<double>& A, std::vector<double>& g)
{
    const size_t n = s.pos.size(), m = n_tuned;
    std::vector<std::vector<double>> At(jobs), gt(jobs);
    std::vector<double> part(jobs, 0);
    std::vector<std::thread> pool;
    for(int t=0;t<jobs;t++)
    pool.emplace_back([&, t]
    {
        At[t].assign(m*m, 0);
        gt[t].assign(m, 0);
        double* H = At[t].data();
        double* gg = gt[t].data();
        std::vector<double> d(n_params, 0);     // de/dth, dense scratch
        std::vector<char> seen(n_params, 0);
        int js[1024];
        std::pair<int,double> J[1024];
        double sum = 0;
        for(size_t i=n*t/jobs;i<n*(t+1)/jobs;i++)
        {
            const Pos& p = s.pos[i];
            const double e = model_eval(p, th, d.data(), 1);
            const double q = sigmoid(e/K), r = q - p.r2*0.5, sp = q*(1-q)/K;
            sum += r*r;
            // the weights the position touches, as model_eval() reads them
            int nj = 0;
            for(int k=0;k<p.n_tab;k++)
            {
                const int idx = (p.w[k>>1] >> (16*(k&1))) & 511;
                js[nj++] = TO_BASE+idx;
                js[nj++] = TE_BASE+idx;
            }
            const uint32_t* w = p.w + (p.n_tab+1)/2;
            for(int k=0;k<p.n_lin+p.n_ks[0]+p.n_ks[1];k++) js[nj++] = w[k] & PARAM_MASK;
            int nJ = 0;
            for(int k=0;k<nj;k++)
            {
                const int j = js[k];
                if(seen[j]) continue;
                seen[j] = 1;
                if(tix[j]>=0 && d[j]!=0) J[nJ++] = {tix[j], d[j]*sp};
            }
            for(int k=0;k<nj;k++) { seen[js[k]] = 0; d[js[k]] = 0; }
            std::sort(J, J+nJ);
            for(int a=0;a<nJ;a++)
            {
                const double va = J[a].second;
                gg[J[a].first] += va*r;
                double* row = H + (size_t)J[a].first*m;
                for(int b=a;b<nJ;b++) row[J[b].first] += va*J[b].second;
            }
        }
        part[t] = sum;
    });
    for(auto& x : pool) x.join();
    A.assign(m*m, 0);
    g.assign(m, 0);
    double sum = 0;
    for(int t=0;t<jobs;t++)
    {
        sum += part[t];
        for(size_t k=0;k<m*m;k++) A[k] += At[t][k];
        for(size_t k=0;k<m;k++) g[k] += gt[t][k];
    }
    for(size_t a=0;a<m;a++)
    {
        for(size_t b=a;b<m;b++) A[a*m+b] /= n;
        A[a*m+a] += lambda;
        g[a] = g[a]/n + lambda*(th[jof[a]]-th0[jof[a]]);
    }
    return sum/n;
}

// In place: the upper triangle of A becomes U with A = U'U. False if A is not positive definite.
static bool cholesky(std::vector<double>& A, int m)
{
    for(int i=0;i<m;i++)
    {
        double* ri = &A[(size_t)i*m];
        if(!(ri[i]>0)) return false;
        const double d = std::sqrt(ri[i]);
        for(int j=i;j<m;j++) ri[j] /= d;
        for(int k=i+1;k<m;k++)
        {
            const double f = ri[k];
            if(f==0) continue;
            double* rk = &A[(size_t)k*m];
            for(int j=k;j<m;j++) rk[j] -= f*ri[j];
        }
    }
    return true;
}

// Solves U'U x = b, b overwritten with x.
static void cholesky_solve(const std::vector<double>& U, int m, double* b)
{
    for(int i=0;i<m;i++)
    {
        const double* ri = &U[(size_t)i*m];
        b[i] /= ri[i];
        for(int j=i+1;j<m;j++) b[j] -= ri[j]*b[i];
    }
    for(int i=m-1;i>=0;i--)
    {
        const double* ri = &U[(size_t)i*m];
        double x = b[i];
        for(int j=i+1;j<m;j++) x -= ri[j]*b[j];
        b[i] = x/ri[i];
    }
}

// (U'U)^-1, full and symmetric, from U.
static std::vector<double> cholesky_inverse(const std::vector<double>& U, int m)
{
    std::vector<double> V(U.size(), 0), acc(m);
    for(int i=0;i<m;i++)                // row i of V = U^-1: x U = e_i
    {
        std::fill(acc.begin(), acc.end(), 0.0);
        double* x = &V[(size_t)i*m];
        for(int k=i;k<m;k++)
        {
            const double* rk = &U[(size_t)k*m];
            x[k] = ((k==i) - acc[k])/rk[k];
            for(int j=k+1;j<m;j++) acc[j] += x[k]*rk[j];
        }
    }
    std::vector<double> S(U.size());
    for(int a=0;a<m;a++)
    for(int b=a;b<m;b++)
    {
        const double* ra = &V[(size_t)a*m];
        const double* rb = &V[(size_t)b*m];
        double s = 0;
        for(int k=b;k<m;k++) s += ra[k]*rb[k];
        S[(size_t)a*m+b] = S[(size_t)b*m+a] = s;
    }
    return S;
}

// The eigenvalues (ascending) and eigenvectors (row k of V for d[k]) of the symmetric m x m
// matrix whose upper triangle A holds: Householder to tridiagonal form, then implicit QL
// (JAMA's tred2 and tql2). O(m^3), a few seconds at m = 1000.
static void symmetric_eigen(const std::vector<double>& A, int m, std::vector<double>& d, std::vector<double>& V)
{
    const int n = m;
    std::vector<double> Z((size_t)n*n), e(n);
    auto z = [&](int i, int j) -> double& { return Z[(size_t)i*n+j]; };
    for(int i=0;i<n;i++) for(int j=0;j<n;j++) z(i, j) = i<=j ? A[(size_t)i*n+j] : A[(size_t)j*n+i];
    d.assign(n, 0);
    for(int j=0;j<n;j++) d[j] = z(n-1, j);
    for(int i=n-1;i>0;i--)
    {
        double scale = 0, h = 0;
        for(int k=0;k<i;k++) scale += std::fabs(d[k]);
        if(scale==0)
        {
            e[i] = d[i-1];
            for(int j=0;j<i;j++) { d[j] = z(i-1, j); z(i, j) = 0; z(j, i) = 0; }
        }
        else
        {
            for(int k=0;k<i;k++) { d[k] /= scale; h += d[k]*d[k]; }
            double f = d[i-1], g = std::sqrt(h);
            if(f>0) g = -g;
            e[i] = scale*g;
            h -= f*g;
            d[i-1] = f-g;
            for(int j=0;j<i;j++) e[j] = 0;
            for(int j=0;j<i;j++)
            {
                f = d[j];
                z(j, i) = f;
                g = e[j] + z(j, j)*f;
                for(int k=j+1;k<=i-1;k++) { g += z(k, j)*d[k]; e[k] += z(k, j)*f; }
                e[j] = g;
            }
            f = 0;
            for(int j=0;j<i;j++) { e[j] /= h; f += e[j]*d[j]; }
            const double hh = f/(h+h);
            for(int j=0;j<i;j++) e[j] -= hh*d[j];
            for(int j=0;j<i;j++)
            {
                f = d[j]; g = e[j];
                for(int k=j;k<=i-1;k++) z(k, j) -= f*e[k] + g*d[k];
                d[j] = z(i-1, j);
                z(i, j) = 0;
            }
        }
        d[i] = h;
    }
    for(int i=0;i<n-1;i++)
    {
        z(n-1, i) = z(i, i);
        z(i, i) = 1;
        const double h = d[i+1];
        if(h!=0)
        {
            for(int k=0;k<=i;k++) d[k] = z(k, i+1)/h;
            for(int j=0;j<=i;j++)
            {
                double g = 0;
                for(int k=0;k<=i;k++) g += z(k, i+1)*z(k, j);
                for(int k=0;k<=i;k++) z(k, j) -= g*d[k];
            }
        }
        for(int k=0;k<=i;k++) z(k, i+1) = 0;
    }
    for(int j=0;j<n;j++) { d[j] = z(n-1, j); z(n-1, j) = 0; }
    z(n-1, n-1) = 1;
    // QL on the rows of V = Z' (the rotations then run along memory)
    V.assign((size_t)n*n, 0);
    for(int i=0;i<n;i++) for(int j=0;j<n;j++) V[(size_t)j*n+i] = z(i, j);
    std::vector<double>().swap(Z);
    for(int i=1;i<n;i++) e[i-1] = e[i];
    e[n-1] = 0;
    double f = 0, tst1 = 0;
    const double eps = std::ldexp(1.0, -52);
    for(int l=0;l<n;l++)
    {
        tst1 = std::max(tst1, std::fabs(d[l])+std::fabs(e[l]));
        int m2 = l;
        while(m2<n && std::fabs(e[m2])>eps*tst1) m2++;
        if(m2>l)
        do
        {
            double g = d[l];
            double p = (d[l+1]-g)/(2*e[l]);
            double r = std::hypot(p, 1.0);
            if(p<0) r = -r;
            d[l] = e[l]/(p+r);
            d[l+1] = e[l]*(p+r);
            const double dl1 = d[l+1];
            double h = g-d[l];
            for(int i=l+2;i<n;i++) d[i] -= h;
            f += h;
            p = d[m2];
            double c = 1, c2 = c, c3 = c, s = 0, s2 = 0;
            const double el1 = e[l+1];
            for(int i=m2-1;i>=l;i--)
            {
                c3 = c2; c2 = c; s2 = s;
                g = c*e[i];
                h = c*p;
                r = std::hypot(p, e[i]);
                e[i+1] = s*r;
                s = e[i]/r;
                c = p/r;
                p = c*d[i] - s*g;
                d[i+1] = h + s*(c*g + s*d[i]);
                double* vi = &V[(size_t)i*n];
                double* vi1 = &V[(size_t)(i+1)*n];
                for(int k=0;k<n;k++) { h = vi1[k]; vi1[k] = s*vi[k] + c*h; vi[k] = c*vi[k] - s*h; }
            }
            p = -s*s2*c3*el1*e[l]/dl1;
            e[l] = s*p;
            d[l] = c*p;
        }
        while(std::fabs(e[l])>eps*tst1);
        d[l] += f;
        e[l] = 0;
    }
    std::vector<int> order(n);
    for(int i=0;i<n;i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return d[a]<d[b]; });
    std::vector<double> ds(n), Vs((size_t)n*n);
    for(int k=0;k<n;k++)
    {
        ds[k] = d[order[k]];
        std::copy(&V[(size_t)order[k]*n], &V[(size_t)order[k]*n]+n, &Vs[(size_t)k*n]);
    }
    d.swap(ds);
    V.swap(Vs);
}

// ---- the solver's coordinates: the tuned weights but one square per pinned table (its
// anchor), which moves by minus the sum of the others' steps, so the mean stays put

static int n_red = 0;
static int red_t[MAX_PARAMS];           // reduced -> tuned
static int red_of[MAX_PARAMS];          // tuned -> reduced, -1 for an anchor
static int anchor_of[MAX_PARAMS];       // tuned -> its table's anchor (tuned), -1 if none
static std::vector<std::vector<int>> pin_groups;   // tuned indices of each pinned table, anchor last

// The normal equations (A upper, nt^2) in reduced coordinates: B'AB, B'g.
static void reduce(const std::vector<double>& A, const std::vector<double>& g, int nt, std::vector<double>& Ar, std::vector<double>& gr)
{
    auto a = [&](int x, int y) { return x<=y ? A[(size_t)x*nt+y] : A[(size_t)y*nt+x]; };
    Ar.assign((size_t)n_red*n_red, 0);
    gr.assign(n_red, 0);
    for(int i=0;i<n_red;i++)
    {
        const int ti = red_t[i], ai = anchor_of[ti];
        gr[i] = g[ti] - (ai>=0 ? g[ai] : 0);
        for(int k=i;k<n_red;k++)
        {
            const int tk = red_t[k], ak = anchor_of[tk];
            double x = a(ti, tk);
            if(ak>=0) x -= a(ti, ak);
            if(ai>=0) x -= a(ai, tk);
            if(ai>=0 && ak>=0) x += a(ai, ak);
            Ar[(size_t)i*n_red+k] = x;
        }
    }
}

// A reduced step z as a step over the tuned weights.
static void expand(const double* z, std::vector<double>& step, int nt)
{
    step.assign(nt, 0);
    for(int i=0;i<n_red;i++)
    {
        step[red_t[i]] += z[i];
        if(anchor_of[red_t[i]]>=0) step[anchor_of[red_t[i]]] -= z[i];
    }
}

// ---- the data

static bool read_split(const std::string& path, Split& s)
{
    std::ifstream in(path);
    if(!in) return false;
    std::string line;
    std::vector<double> results;
    while(std::getline(in, line))
    {
        if(line.empty() || line[0]=='#') continue;
        // game ply fen static qsearch result ...
        size_t a = line.find('\t'), b = line.find('\t', a+1), c = line.find('\t', b+1);
        size_t d = line.find('\t', c+1), e = line.find('\t', d+1), f = line.find('\t', e+1);
        if(f==std::string::npos) { std::fprintf(stderr, "%s: bad line %s\n", path.c_str(), line.c_str()); return false; }
        s.blob.append(line, b+1, c-b-1);
        s.fen_at.push_back((uint32_t)s.blob.size());
        results.push_back(std::atof(line.substr(e+1, f-e-1).c_str()));
    }
    s.blob.shrink_to_fit();
    s.fen_at.shrink_to_fit();
    s.pos.assign(s.count(), Pos{});
    s.base.assign(s.count(), 0);
    for(size_t i=0;i<results.size();i++) s.pos[i].r2 = (uint8_t)std::lround(2*results[i]);
    return s.count()>0;
}

// Features of every position of s, in jobs threads, each into blocks of its own;
// with features false only basic_eval() (the test split needs nothing else).
static void make_features(Split& s, int jobs, bool features)
{
    const size_t n = s.count();
    std::vector<std::vector<std::unique_ptr<uint32_t[]>>> blocks(jobs);
    std::vector<size_t> words_of(jobs, 0), differ(jobs, 0), dsum(jobs, 0), big(jobs, 0);
    std::vector<int> worst(jobs, 0);
    std::vector<std::thread> pool;
    for(int t=0;t<jobs;t++)
    pool.emplace_back([&, t]
    {
        std::vector<uint32_t> words;
        size_t used = BLOCK_WORDS;
        for(size_t i=n*t/jobs;i<n*(t+1)/jobs;i++)
        {
            BB b;
            const std::string fen = s.fen(i);
            if(!uci_parse_fen(fen, b)) { std::fprintf(stderr, "bad FEN %s\n", fen.c_str()); std::exit(1); }
            s.base[i] = basic_eval(&b, W_FROM);
            if(W_CHECK)
            {
                const int d = std::abs(basic_eval(&b, *W_CHECK) - s.base[i]);
                worst[t] = std::max(worst[t], d);
                differ[t] += d!=0;
                big[t] += d>4;
                dsum[t] += d;
            }
            if(!features)
            continue;
            words.clear();
            s.pos[i] = make_pos(b, s.pos[i].r2*0.5, words);
            if(used + words.size() > BLOCK_WORDS)
            {
                blocks[t].emplace_back(new uint32_t[BLOCK_WORDS]);
                used = 0;
            }
            uint32_t* at = blocks[t].back().get() + used;
            std::copy(words.begin(), words.end(), at);
            s.pos[i].w = at;
            used += words.size();
            words_of[t] += words.size();
        }
    });
    for(auto& th : pool) th.join();
    for(int t=0;t<jobs;t++)
    {
        for(auto& b : blocks[t]) s.blocks.push_back(std::move(b));
        s.n_words += words_of[t];
        check_worst = std::max(check_worst, worst[t]);
        check_differ += differ[t];
        check_big += big[t];
        check_sum += dsum[t];
    }
    if(W_CHECK) check_n += n;
}

// Mean loss of the real basic_eval() with W over the split in `path`.
static bool read_split(const std::string& path, Split& s);
static double real_loss(const std::string& path, const WEIGHTS& W, double K, int jobs)
{
    Split s;
    if(!read_split(path, s)) { std::fprintf(stderr, "cannot read %s again\n", path.c_str()); std::exit(1); }
    const size_t n = s.count();
    std::vector<double> part(jobs, 0);
    std::vector<std::thread> pool;
    for(int t=0;t<jobs;t++)
    pool.emplace_back([&, t]
    {
        double sum = 0;
        for(size_t i=n*t/jobs;i<n*(t+1)/jobs;i++)
        {
            BB b;
            uci_parse_fen(s.fen(i), b);
            const double q = sigmoid(basic_eval(&b, W)/K), r = s.pos[i].r2*0.5;
            sum += (q-r)*(q-r);
        }
        part[t] = sum;
    });
    for(auto& th : pool) th.join();
    double sum = 0;
    for(double x : part) sum += x;
    return sum/n;
}

__attribute__((format(printf, 2, 3)))
static void say(std::string& report, const char* fmt, ...)
{
    char line[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    report += line;
    report += "\n";
}

static double base_loss(const Split& s, double K)
{
    double sum = 0;
    for(size_t i=0;i<s.pos.size();i++)
    {
        const double q = sigmoid(s.base[i]/K), r = s.pos[i].r2*0.5;
        sum += (q-r)*(q-r);
    }
    return sum/s.pos.size();
}

int main(int argc, char** argv)
{
    Options opt;
    for(int i=1;i<argc;i++)
    {
        std::string a = argv[i];
        const size_t eq = a.find('=');
        if(eq==std::string::npos) { std::fprintf(stderr, "options are key=value: %s\n", a.c_str()); return 2; }
        const std::string k = a.substr(0, eq), v = a.substr(eq+1);
        if(k=="data") opt.data = v;
        else if(k=="from") opt.from = v;
        else if(k=="out") opt.out = v;
        else if(k=="tag") opt.tag = v;
        else if(k=="jobs") opt.jobs = std::max(1, std::atoi(v.c_str()));
        else if(k=="epochs") opt.epochs = std::max(0, std::atoi(v.c_str()));
        else if(k=="lr") opt.lr = std::atof(v.c_str());
        else if(k=="lambda") opt.lambda = std::atof(v.c_str());
        else if(k=="k") opt.k = std::atof(v.c_str());
        else if(k=="method") opt.method = v;
        else if(k=="iters") opt.iters = std::max(0, std::atoi(v.c_str()));
        else if(k=="mu") opt.mu = std::atof(v.c_str());
        else if(k=="tol") opt.tol = std::atof(v.c_str());
        else if(k=="se") opt.se = std::atoi(v.c_str())!=0;
        else if(k=="check") opt.check = std::atoi(v.c_str())!=0;
        else if(k=="compare") opt.compare = v;
        else if(k=="spectrum") opt.spectrum = v;
        else if(k=="gauge" && (v=="pin" || v=="pin104" || v=="free")) { opt.pin = v!="free"; opt.pin3 = v=="pin"; }
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    if(opt.method!="adam" && opt.method!="gn") { std::fprintf(stderr, "method is adam or gn\n"); return 2; }
    if(opt.lambda<0) { std::fprintf(stderr, "lambda >= 0\n"); return 2; }

    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    const auto t0 = std::chrono::steady_clock::now();
    auto secs = [&]{ return std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count(); };

    Weight_Set_Info from_info;
    std::string error;
    W_FROM = WEIGHTS_OG;
    if(!load_weight_set(opt.from, W_FROM, from_info, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    if(opt.out.empty()) opt.out = "weights/w" + std::to_string(W_FROM.version+1) + ".txt";
    const WEIGHTS W_READ = W_FROM;
    PIN_DEGENERATE = opt.pin3;
    if(opt.pin)
    {
        regauge(W_FROM);
        W_CHECK = &W_READ;
    }
    build_params(W_FROM);
    if(opt.pin)
    for(int piece=1;piece<=4;piece++)
    for(int ph=0;ph<2;ph++)
    param_tuned[flat(W_FROM, mobility_table(W_FROM, piece, ph) + MOB_PIN[piece])] = false;
    if(opt.pin3)
    for(const int* p : {&W_FROM.activity_knight_defend, &W_FROM.passed_pawn_value[6], &W_FROM.ks_shelter[GAUGE_KS_SHELTER]})
    param_tuned[flat(W_FROM, p)] = false;
    build_groups();
    TO_BASE = flat(W_FROM, &W_FROM.piece_table_value_opening[0][0]);
    TE_BASE = flat(W_FROM, &W_FROM.piece_table_value_endgame[0][0]);

    Split split[3];
    const char* names[3] = {"train", "valid", "test"};
    for(int s=0;s<3;s++)
    {
        if(!read_split(opt.data + "/" + names[s] + ".tsv", split[s])) { std::fprintf(stderr, "cannot read %s/%s.tsv\n", opt.data.c_str(), names[s]); return 1; }
        make_features(split[s], opt.jobs, s<2);
        std::string().swap(split[s].blob);   // fen() is gone from here on
        std::printf("%s: %zu positions, %.1f words each (%.1f s)\n", names[s], split[s].pos.size(),
                    (double)split[s].n_words/split[s].pos.size(), secs());
        std::fflush(stdout);
    }

    std::vector<double> th0(n_params);
    for(int j=0;j<n_params;j++) th0[j] = at(W_FROM, j);
    char gauge_line[512] = "gauge free: the table means and mobility constants are held by lambda alone";
    if(opt.pin)
    {
        std::string v = "";
        for(int ph=0;ph<2;ph++)
        for(int piece=0;piece<5;piece++)
        v += " " + std::to_string((ph ? W_FROM.piece_value_endgame : W_FROM.piece_value)[piece]) + (piece==4 && !ph ? " /" : "");
        std::snprintf(gauge_line, sizeof gauge_line, "gauge pinned, set %d rewritten: basic_eval() differs on %zu of %zu positions (mean |diff| %.3f cp, max %d, %zu beyond 4); values P R N B Q%s",
                      W_FROM.version, check_differ, check_n, check_n ? (double)check_sum/check_n : 0.0, check_worst, check_big, v.c_str());
    }
    std::printf("%s\n", gauge_line);

    // the model against the real eval, set `from`
    {
        double sum = 0; int worst = 0;
        const Split& s = split[0];
        for(size_t i=0;i<s.pos.size();i++)
        {
            const double diff = std::fabs(model_eval(s.pos[i], th0.data(), nullptr, 0) - s.base[i]);
            sum += diff;
            worst = std::max(worst, (int)std::ceil(diff));
        }
        std::printf("model vs basic_eval() on train, set %d: mean |diff| %.2f cp, max %d cp\n", W_FROM.version, sum/s.pos.size(), worst);
    }

    // K, once, on the real eval of set `from`
    double K = opt.k;
    if(K<=0)
    {
        double lo = 10, hi = 2000;
        const double gr = (std::sqrt(5.0)-1)/2;
        double a = hi - gr*(hi-lo), b = lo + gr*(hi-lo);
        double fa = base_loss(split[0], a), fb = base_loss(split[0], b);
        while(hi-lo > 0.01)
        {
            if(fa<fb) { hi = b; b = a; fb = fa; a = hi - gr*(hi-lo); fa = base_loss(split[0], a); }
            else      { lo = a; a = b; fa = fb; b = lo + gr*(hi-lo); fb = base_loss(split[0], b); }
        }
        K = (lo+hi)/2;
    }
    std::printf("K = %.2f (train loss of set %d: %.6f)\n", K, W_FROM.version, base_loss(split[0], K));

    // the weights no train position touches are held at `from`
    std::vector<char> touched(n_params, 0);
    std::vector<size_t> seen(n_params, 0);
    for(const Pos& p : split[0].pos)
    {
        for(int k=0;k<p.n_tab;k++)
        {
            const int idx = (p.w[k>>1] >> (16*(k&1))) & 511;
            touched[TO_BASE+idx] = touched[TE_BASE+idx] = 1;
            seen[TO_BASE+idx]++; seen[TE_BASE+idx]++;
        }
        const uint32_t* w = p.w + (p.n_tab+1)/2;
        for(int k=0;k<p.n_lin+p.n_ks[0]+p.n_ks[1];k++) { touched[w[k] & PARAM_MASK] = 1; seen[w[k] & PARAM_MASK]++; }
    }
    int untouched = 0;
    for(int j=0;j<n_params;j++)
    {
        untouched += param_tuned[j] && !touched[j];
        tix[j] = param_tuned[j] && touched[j] ? n_tuned : -1;
        if(tix[j]>=0) jof[n_tuned++] = j;
    }
    const int nt = n_tuned;
    {
        std::string modes;
        for(int piece=1;piece<=4;piece++)
        {
            const int j0 = flat(W_FROM, mobility_table(W_FROM, piece, 0));
            int top = 0;
            for(int k=1;k<MOB_SIZE[piece];k++) if(seen[j0+k]>seen[j0+top]) top = k;
            modes += std::string(" ") + "RNBQ"[piece-1] + " " + std::to_string(top);
        }
        std::printf("tuned: %d weights, %d more no train position touches; the most frequent mobility counts:%s\n", nt, untouched, modes.c_str());
    }
    // the pinned tables, and the reduced coordinates
    for(int t=0;t<nt;t++) anchor_of[t] = -1;
    if(opt.pin)
    for(int piece=0;piece<6;piece++)
    for(int ph=0;ph<2;ph++)
    {
        std::vector<int> G;
        for(int sq=0;sq<64;sq++)
        if(on_table(piece, sq) && tix[(ph ? TE_BASE : TO_BASE) + 64*piece + sq]>=0) G.push_back(tix[(ph ? TE_BASE : TO_BASE) + 64*piece + sq]);
        if(G.size()<2) continue;
        for(size_t k=0;k+1<G.size();k++) anchor_of[G[k]] = G.back();
        pin_groups.push_back(G);
    }
    for(int t=0;t<nt;t++) red_of[t] = -1;
    for(int t=0;t<nt;t++)
    {
        bool anchor = false;
        for(const auto& G : pin_groups) anchor |= G.back()==t;
        if(!anchor) { red_of[t] = n_red; red_t[n_red++] = t; }
    }
    auto regular = [&](const double* x) { double r = 0; for(int t=0;t<nt;t++) r += (x[jof[t]]-th0[jof[t]])*(x[jof[t]]-th0[jof[t]]); return opt.lambda*r; };

    std::vector<double> th = th0, best = th0;
    double best_valid = model_loss(split[1], th.data(), K, opt.jobs, nullptr);
    int best_epoch = 0, steps = 0;
    std::printf("epoch 0: train %.6f valid %.6f\n", model_loss(split[0], th.data(), K, opt.jobs, nullptr), best_valid);
    if(opt.method=="adam")
    {
        // full-batch Adam; a pinned table's step loses its mean
        std::vector<double> grad(n_params), m(n_params, 0), v(n_params, 0), prev;
        const double b1 = 0.9, b2 = 0.999;
        for(int ep=1; ep<=opt.epochs; ep++)
        {
            const double train = model_loss(split[0], th.data(), K, opt.jobs, grad.data());
            const double lr = opt.lr*(0.01 + 0.99*0.5*(1+std::cos(M_PI*(ep-1)/opt.epochs)));
            prev = th;
            for(int j=0;j<n_params;j++)
            {
                if(tix[j]<0) continue;
                const double gj = grad[j] + 2*opt.lambda*(th[j]-th0[j]);
                m[j] = b1*m[j] + (1-b1)*gj;
                v[j] = b2*v[j] + (1-b2)*gj*gj;
                const double mh = m[j]/(1-std::pow(b1, ep)), vh = v[j]/(1-std::pow(b2, ep));
                if(vh>0) th[j] -= lr*mh/(std::sqrt(vh)+1e-12);
            }
            for(const auto& G : pin_groups)
            {
                double mean = 0;
                for(int t : G) mean += th[jof[t]]-prev[jof[t]];
                mean /= G.size();
                for(int t : G) th[jof[t]] -= mean;
            }
            const double valid = model_loss(split[1], th.data(), K, opt.jobs, nullptr);
            if(valid<best_valid) { best_valid = valid; best = th; best_epoch = ep; }
            if(ep%50==0 || ep==opt.epochs)
            {
                int bad = 0;
                for(int j=0;j<n_params;j++) bad += !std::isfinite(grad[j]) || !std::isfinite(th[j]);
                std::printf("epoch %d: train %.6f valid %.6f lr %.3f%s (%.0f s)\n", ep, train, valid, lr,
                            bad ? (" " + std::to_string(bad) + " weights not finite").c_str() : "", secs());
                std::fflush(stdout);
            }
        }
    }
    else
    {
        // Levenberg-Marquardt: (A + mu diag A) step = -g in reduced coordinates, kept if the objective falls
        std::vector<double> A, g, Ar, gr, U, z(n_red), step, tn;
        double F = model_loss(split[0], th.data(), K, opt.jobs, nullptr) + regular(th.data());
        double mu = opt.mu;
        for(int it=1; it<=opt.iters; it++)
        {
            normal_eqs(split[0], th.data(), th0.data(), K, opt.lambda, opt.jobs, A, g);
            reduce(A, g, nt, Ar, gr);
            if(opt.check && it==1)
            {
                // Adam's gradient is twice g
                std::vector<double> grad(n_params);
                model_loss(split[0], th.data(), K, opt.jobs, grad.data());
                double worst = 0, big = 0;
                for(int t=0;t<nt;t++)
                {
                    const int j = jof[t];
                    const double ga = grad[j] + 2*opt.lambda*(th[j]-th0[j]);
                    worst = std::max(worst, std::fabs(2*g[t]-ga));
                    big = std::max(big, std::fabs(ga));
                }
                std::printf("check: max |2 g - Adam's gradient| %.3g, max |gradient| %.3g\n", worst, big);
                for(int j : {flat(W_FROM, &W_FROM.piece_value[1]), TO_BASE+64+27, flat(W_FROM, &W_FROM.ks_hit), flat(W_FROM, &W_FROM.threat_hanging)})
                std::printf("  %-34s gn %.6e  adam %.6e\n", param_name[j].c_str(), 2*g[tix[j]], grad[j] + 2*opt.lambda*(th[j]-th0[j]));
            }
            bool taken = false;
            double Fn = F;
            for(int tries=0; tries<20 && !taken; tries++)
            {
                U = Ar;
                for(int i=0;i<n_red;i++) U[(size_t)i*n_red+i] *= 1+mu;
                if(!cholesky(U, n_red)) { mu *= 10; continue; }
                for(int i=0;i<n_red;i++) z[i] = -gr[i];
                cholesky_solve(U, n_red, z.data());
                expand(z.data(), step, nt);
                tn = th;
                for(int t=0;t<nt;t++) tn[jof[t]] += step[t];
                Fn = model_loss(split[0], tn.data(), K, opt.jobs, nullptr) + regular(tn.data());
                if(Fn<F) taken = true;
                else mu *= 5;
            }
            if(!taken) { std::printf("iteration %d: no step lowers the loss (mu %.1e)\n", it, mu); break; }
            double biggest = 0;
            for(int t=0;t<nt;t++) biggest = std::max(biggest, std::fabs(step[t]));
            const double gain = F-Fn;
            th = tn;
            F = Fn;
            steps = it;
            mu = std::max(mu/5, 1e-12);
            const double valid = model_loss(split[1], th.data(), K, opt.jobs, nullptr);
            if(valid<best_valid) { best_valid = valid; best = th; best_epoch = it; }
            std::printf("iteration %d: train %.6f valid %.6f mu %.1e largest step %.1f (%.0f s)\n", it, F-regular(th.data()), valid, mu, biggest, secs());
            std::fflush(stdout);
            if(gain < opt.tol*F) break;
        }
    }

    // standard errors at the kept point: sigma^2/n (J'J/n + lambda I)^-1 in reduced
    // coordinates, and again with 10 lambda, to tell what the data holds from what lambda holds
    std::vector<double> se(nt, 0), se_c(nt, 0);
    std::vector<int> se_flag(nt, 0);    // 2: held by lambda
    std::vector<std::vector<std::pair<int,double>>> contrast(nt);
    double sigma2 = 0, ridge = 0, weakest = 0, top_diag = 0;
    std::string weakest_dir;
    if(opt.se)
    {
        std::vector<double> A, g, Ar, gr;
        sigma2 = normal_eqs(split[0], best.data(), th0.data(), K, opt.lambda, opt.jobs, A, g);
        const double n = split[0].pos.size();
        std::vector<double> S[2];
        for(int pass=0; pass<(opt.lambda>0 ? 2 : 1); pass++)
        {
            std::vector<double> Ap = A;
            for(int t=0;t<nt;t++) Ap[(size_t)t*nt+t] += pass*9*opt.lambda;
            reduce(Ap, g, nt, Ar, gr);
            for(int i=0;i<n_red;i++) top_diag = std::max(top_diag, Ar[(size_t)i*n_red+i]);
            std::vector<double> U = Ar;
            double r = 0;
            while(!cholesky(U, n_red))
            {
                r = r ? r*10 : 1e-15*top_diag;
                if(r > 1e-3*top_diag) { std::fprintf(stderr, "the normal equations are not positive definite\n"); return 1; }
                U = Ar;
                for(int i=0;i<n_red;i++) U[(size_t)i*n_red+i] += r;
            }
            if(pass==0)
            {
                ridge = r;
                // the whole spectrum of A (#107); the weakest WEAK_DIRECTIONS in the report, each
                // with the SE of the weights' combination along it, sqrt(sigma^2/(n eigenvalue))
                std::vector<double> ev, V;
                symmetric_eigen(Ar, n_red, ev, V);
                const double sigma2n = sigma2/n;
                double residual = 0;
                std::FILE* spec = opt.spectrum.empty() ? nullptr : std::fopen(opt.spectrum.c_str(), "w");
                if(spec) std::fprintf(spec, "# k eigenvalue direction_SE components (the 8 largest, of the tuned weights)\n");
                for(int v=0;v<n_red;v++)
                {
                    const double* x = &V[(size_t)v*n_red];
                    const bool report_it = v<WEAK_DIRECTIONS;
                    if(!report_it && !spec) break;
                    if(report_it)
                    for(int i=0;i<n_red;i++)
                    {
                        double y = 0;
                        for(int k=0;k<n_red;k++) y += (i<=k ? Ar[(size_t)i*n_red+k] : Ar[(size_t)k*n_red+i])*x[k];
                        residual = std::max(residual, std::fabs(y-ev[v]*x[i]));
                    }
                    if(v==0) weakest = ev[v];
                    std::vector<double> st;
                    expand(x, st, nt);
                    std::vector<std::pair<double,int>> big;
                    for(int t=0;t<nt;t++) big.push_back({-std::fabs(st[t]), t});
                    std::sort(big.begin(), big.end());
                    const double dse = ev[v]>0 ? std::sqrt(sigma2n/ev[v]) : INFINITY;
                    std::string text;
                    char part[160];
                    for(int k=0;k<8 && k<nt;k++)
                    {
                        std::snprintf(part, sizeof part, "%s %+.2f %s", k ? "," : "", st[big[k].second], param_name[jof[big[k].second]].c_str());
                        text += part;
                    }
                    if(report_it)
                    {
                        std::snprintf(part, sizeof part, "%s  %.2e (SE %.3g):", v ? "\n" : "", ev[v], dse);
                        weakest_dir += part + text;
                    }
                    if(spec) std::fprintf(spec, "%d %.6e %.6g%s\n", v, ev[v], dse, text.c_str());
                }
                if(spec) std::fclose(spec);
                std::printf("eigenvalues: %d, smallest %.3g, largest %.3g, residual of the weakest %d %.2g (%.0f s)\n",
                            n_red, ev[0], ev[n_red-1], std::min(WEAK_DIRECTIONS, n_red), residual, secs());
            }
            S[pass] = cholesky_inverse(U, n_red);
            for(double& x : S[pass]) x *= sigma2/n;
        }
        // a tuned weight in reduced coordinates: an anchor is minus the rest of its table
        auto to_red = [&](int t, double c, std::vector<std::pair<int,double>>& out)
        {
            if(red_of[t]>=0) { out.push_back({red_of[t], c}); return; }
            for(const auto& G : pin_groups) if(G.back()==t) for(size_t k=0;k+1<G.size();k++) out.push_back({red_of[G[k]], -c});
        };
        for(int t=0;t<nt;t++) contrast[t] = {{t, 1.0}};
        if(!opt.pin)
        {
            // gauge-free contrasts: a table or mobility entry against its table's mean, a piece
            // value plus the means of its tables
            auto members = [&](int j0, int count) { std::vector<int> v; for(int k=0;k<count;k++) if(tix[j0+k]>=0) v.push_back(tix[j0+k]); return v; };
            auto against_mean = [&](const std::vector<int>& T)
            {
                for(int t : T) for(int u : T) contrast[t].push_back({u, -1.0/T.size()});
            };
            for(int piece=0;piece<6;piece++)
            for(int ph=0;ph<2;ph++)
            {
                const std::vector<int> T = members((ph ? TE_BASE : TO_BASE) + 64*piece, 64);
                const std::vector<int> M = mobility_table(W_FROM, piece, ph) ? members(flat(W_FROM, mobility_table(W_FROM, piece, ph)), MOB_SIZE[piece]) : std::vector<int>();
                const int v = tix[flat(W_FROM, ph ? &W_FROM.piece_value_endgame[piece] : &W_FROM.piece_value[piece])];
                if(v>=0)
                {
                    for(int u : T) contrast[v].push_back({u, 1.0/T.size()});
                    for(int u : M) contrast[v].push_back({u, 1.0/M.size()});
                }
                against_mean(T);
                against_mean(M);
            }
        }
        auto var = [&](const std::vector<double>& Sp, const std::vector<std::pair<int,double>>& c)
        {
            std::vector<std::pair<int,double>> r;
            for(auto [t, ct] : c) to_red(t, ct, r);
            double s = 0;
            for(auto [a, ca] : r) for(auto [b, cb] : r) s += ca*cb*Sp[(size_t)a*n_red+b];
            return s;
        };
        for(int t=0;t<nt;t++)
        {
            se[t] = std::sqrt(var(S[0], {{t, 1.0}}));
            se_c[t] = std::sqrt(var(S[0], contrast[t]));
            if(opt.lambda>0 && se_c[t] > 1.5*std::sqrt(var(S[1], contrast[t]))) se_flag[t] = 2;
        }
        std::printf("standard errors (%.0f s)\n", secs());
    }
    // the new set: rounded, then judged by the real eval
    WEIGHTS W_NEW = W_FROM;
    for(int j=0;j<n_params;j++)
    if(param_tuned[j]) at(W_NEW, j) = (int)std::lround(best[j]);
    W_NEW.version = W_FROM.version+1;
    double loss[2][3];
    for(int s=0;s<3;s++)
    std::vector<std::unique_ptr<uint32_t[]>>().swap(split[s].blocks);
    for(int s=0;s<3;s++)
    {
        loss[0][s] = base_loss(split[s], K);
        loss[1][s] = real_loss(opt.data + "/" + names[s] + ".tsv", W_NEW, K, opt.jobs);
    }

    char line[512];
    std::string report;
    say(report, "tools/tune %s: set %d from set %d, %s", ENGINE_COMMIT, W_NEW.version, W_FROM.version, opt.data.c_str());
    say(report, "positions: train %zu, valid %zu, test %zu", split[0].pos.size(), split[1].pos.size(), split[2].pos.size());
    if(opt.method=="adam")
    say(report, "K %.2f (fitted on set %d), lambda %g, adam, lr %g, epochs %d, best epoch %d (valid, model %.6f), %.0f s",
        K, W_FROM.version, opt.lambda, opt.lr, opt.epochs, best_epoch, best_valid, secs());
    else
    say(report, "K %.2f (fitted on set %d), lambda %g, gn (Levenberg-Marquardt), %d steps, best step %d (valid, model %.6f), %.0f s",
        K, W_FROM.version, opt.lambda, steps, best_epoch, best_valid, secs());
    say(report, "%s", gauge_line);
    say(report, "tuned: %d weights; %d no train position touches, held", nt, untouched);
    say(report, "loss (sigma(basic_eval/K) - result)^2, real eval:");
    say(report, "  set   train      valid      test");
    for(int k=0;k<2;k++)
    say(report, "  %-4d  %.6f   %.6f   %.6f", k ? W_NEW.version : W_FROM.version, loss[k][0], loss[k][1], loss[k][2]);

    // the largest changes, scalars and table entries apart
    std::vector<std::pair<double,int>> moved;
    for(int j=0;j<n_params;j++)
    if(param_tuned[j] && at(W_NEW, j)!=at(W_FROM, j)) moved.push_back({-std::fabs((double)at(W_NEW, j)-at(W_FROM, j)), j});
    std::sort(moved.begin(), moved.end());
    say(report, "changed: %zu of %d weights; scalars:", moved.size(), n_params);
    for(auto& [d, j] : moved)
    if(param_name[j].rfind("piece_table", 0)!=0)
    say(report, "  %-34s %6d -> %6d", param_name[j].c_str(), at(W_FROM, j), at(W_NEW, j));

    // standard errors by weight group, and the differences to set `compare` they explain
    std::string se_lines;
    if(opt.se)
    {
        WEIGHTS W_CMP = W_FROM;
        Weight_Set_Info cmp_info;
        const bool cmp = !opt.compare.empty();
        if(cmp && !load_weight_set(opt.compare, W_CMP, cmp_info, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        if(cmp && opt.pin) regauge(W_CMP);
        auto value = [&](WEIGHTS& W, int t) { double x = 0; for(auto [u, c] : contrast[t]) x += c*at(W, jof[u]); return x; };
        auto group = [&](int j) { return param_name[j].substr(0, param_name[j].find('[')); };
        if(opt.pin)
        say(report, "standard errors (the gauge is pinned: SE* = SE), sigma^2 %.6f", sigma2);
        else
        say(report, "standard errors (* = gauge-free: a table or mobility entry against its table's mean, a piece value plus those means), sigma^2 %.6f", sigma2);
        char ridge_text[32];
        std::snprintf(ridge_text, sizeof ridge_text, "%.0e", top_diag ? ridge/top_diag : 0.0);
        say(report, "weakest directions (the %d smallest eigenvalues of A, the first %.3g, and the SE along each), largest diagonal %.3g, lambda %g%s:",
            std::min(WEAK_DIRECTIONS, n_red), weakest, top_diag, opt.lambda,
            ridge ? (", singular: SEs with a ridge of " + std::string(ridge_text) + " * largest diagonal").c_str() : "");
        report += weakest_dir + "\n";
        say(report, "  %-34s %5s %8s %8s %6s%s", "group", "n", "median", "median*", "lambda", cmp ? "  >2 SE* vs compare" : "");
        std::vector<std::string> order;
        for(int t=0;t<nt;t++) if(std::find(order.begin(), order.end(), group(jof[t]))==order.end()) order.push_back(group(jof[t]));
        int total = 0, beyond2 = 0, beyond3 = 0, held = 0;
        for(const std::string& gname : order)
        {
            std::vector<double> a, b;
            int n_lambda = 0, n_beyond = 0;
            for(int t=0;t<nt;t++)
            {
                if(group(jof[t])!=gname) continue;
                a.push_back(se[t]); b.push_back(se_c[t]);
                n_lambda += se_flag[t]==2;
                const double z = std::fabs(value(W_NEW, t)-value(W_CMP, t))/se_c[t];
                n_beyond += z>2;
                if(cmp) { total++; beyond2 += z>2; beyond3 += z>3; }
            }
            held += n_lambda;
            if(a.empty()) continue;
            std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
            say(report, "  %-34s %5zu %8.2f %8.2f %6d%s", gname.c_str(), a.size(), a[a.size()/2], b[b.size()/2], n_lambda,
                cmp ? ("  " + std::to_string(n_beyond)).c_str() : "");
        }
        say(report, "lambda: SE* shrinks by more than 1.5 with 10 lambda (%d weights); %d weights have no data", held, untouched);
        if(cmp)
        say(report, "set %d vs set %d: %d of %d gauge-free differences exceed 2 SE*, %d exceed 3", W_CMP.version, W_NEW.version, beyond2, total, beyond3);
        se_lines = "\n# standard errors: weight, value, SE, SE* (see the report), fires (train positions and sides with a nonzero coefficient), flag\n";
        for(int t=0;t<nt;t++)
        {
            std::snprintf(line, sizeof line, "# se %-34s %6d %8.2f %8.2f %8zu%s\n", param_name[jof[t]].c_str(), at(W_NEW, jof[t]), se[t], se_c[t],
                          seen[jof[t]], se_flag[t]==2 ? " lambda" : "");
            se_lines += line;
        }
        for(int j=0;j<n_params;j++)
        if(param_tuned[j] && tix[j]<0)
        {
            std::snprintf(line, sizeof line, "# se %-34s %6d        -        -        0 no-data\n", param_name[j].c_str(), at(W_NEW, j));
            se_lines += line;
        }
    }
    std::fputs(report.c_str(), stdout);

    Weight_Set_Info info;
    info.parent = W_FROM.version;
    info.data = opt.tag;
    info.tuner = ENGINE_COMMIT;
    std::snprintf(line, sizeof line, "%.6f", loss[1][1]);
    info.loss = line;
    std::string text = write_weight_set(W_NEW, info) + "\n# fit report (loss = valid loss by the real eval)\n";
    std::istringstream rep(report);
    for(std::string l; std::getline(rep, l); ) text += "# " + l + "\n";
    text += se_lines;
    std::ofstream out(opt.out);
    out << text;
    if(!out) { std::fprintf(stderr, "cannot write %s\n", opt.out.c_str()); return 1; }
    std::printf("wrote %s\n", opt.out.c_str());
    return 0;
}
