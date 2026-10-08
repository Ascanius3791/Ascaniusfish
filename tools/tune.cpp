// OWNERSHIP=Claude
// tools/tune: fits a weight set to game results (issue #87, docs/TUNING_PLAN.md "Method").
//
//   ./tools/tune [data=data/tune] [from=weights/w1.txt] [out=weights/w<from+1>.txt]
//                [jobs=6] [lambda=1e-9] [k=0] [tag=ccrl4040-3000-balanced]
//                [method=gn] [iters=20] [mu=1e-3] [tol=1e-7] [check=0]   (Levenberg-Marquardt)
//                [method=adam] [epochs=1500] [lr=2]                      (Adam)
//                [se=1] [compare=<set>]
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
// ks_full_piece_material (divisors). The table means against the piece values and
// the king table (one king a side: a constant cancels) are degenerate; lambda keeps
// such directions at the `from` set. It also holds back the rarely seen weights:
// lambda=0 fits valid best, but rare table squares and ks_storm entries run out to
// +-400 cp; 1e-9 keeps the tables within +-150 for most of the gain
// (docs/measurements/tune_w2_2026-10-04.md). A fit from its own result is therefore a
// new fit, not more of the old one: the pull moves to the new start, and the loss falls
// again (w7 = w6 refitted twice, #97). One run does reach its minimum (#104).
//
// Two methods (#104), both keeping the step with the lowest valid loss of the model.
// Both reach the same minimum; gn in a sixth of the time (docs/measurements/tune_gn_2026-10-08.md).
// - adam: full-batch Adam, the learning rate falling from lr to lr/100 (cosine).
// - gn (default): Levenberg-Marquardt on the same objective F = mean (q_i - r_i)^2 + lambda |w - w_from|^2,
//   q_i = sigma(e_i/K). Per position J_i = q_i (1 - q_i)/K de_i/dw (model_eval()'s gradient
//   path), so F's Gauss-Newton Hessian is 2 A and its gradient 2 g with A = J'J/n + lambda I,
//   g = J'(q - r)/n + lambda (w - w_from), over the tuned weights only. A step solves
//   (A + mu diag A) d = -g by Cholesky; it is kept if F falls (mu / 5), else mu * 5 and
//   again. Stops after iters steps or when one gains less than tol * F. Needs lambda > 0
//   (weights no position touches make J'J singular). check=1 prints g
//   against Adam's gradient once (2 g = Adam's to rounding).
// The weights are rounded, and the report gives train/valid/test loss of both sets by
// the real basic_eval(). It is printed and written as '#' lines at the end of the new set.
//
// Standard errors (se=1), at the kept point, both methods: Cov(w) ~ sigma^2/n A^-1,
// sigma^2 the mean squared residual on train. That treats the residuals as independent
// with one variance (they are not: positions of one game share a result, and a result's
// variance depends on q), so the SEs are a lower bound of the sampling noise. SE is the
// plain sqrt(Cov_jj). SE* is gauge-free: a table or mobility entry against the mean of its
// table, a piece value plus the means of its square table and mobility table (a constant
// moved between them leaves the eval alone, so only lambda holds their split, and the
// plain SEs of those weights are mostly lambda's). lambda is a prior of sd
// sqrt(sigma^2/(n lambda)) around `from` (6.8 cp on tune2 at 1e-9), so it caps every SE.
// A weight is flagged "lambda" when its SE* shrinks by more than 1.5 with 10 lambda: the
// prior gives it more than about 1/7 of its precision. "no-data": no position touches it. The king tables' constant
// (one king a side) has no gauge-free form: only the entries against their mean mean
// anything there. They are written as '# se' lines after the report. compare=<set>
// counts the gauge-free differences between that set and the new one beyond 2 SE*.
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
};

// ---- the parameters: every number of a weight set, flat, in weight_fields() order

static const int MAX_PARAMS = 4096;
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
    std::vector<size_t> words_of(jobs, 0);
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
    }
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
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    if(opt.method!="adam" && opt.method!="gn") { std::fprintf(stderr, "method is adam or gn\n"); return 2; }
    if(opt.lambda<=0 && (opt.method=="gn" || opt.se)) { std::fprintf(stderr, "method=gn and se=1 need lambda > 0\n"); return 2; }

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
    build_params(W_FROM);
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

    for(int j=0;j<n_params;j++)
    {
        tix[j] = param_tuned[j] ? n_tuned : -1;
        if(param_tuned[j]) jof[n_tuned++] = j;
    }
    const int nt = n_tuned;
    auto regular = [&](const double* x) { double r = 0; for(int t=0;t<nt;t++) r += (x[jof[t]]-th0[jof[t]])*(x[jof[t]]-th0[jof[t]]); return opt.lambda*r; };

    std::vector<double> th = th0, best = th0;
    double best_valid = model_loss(split[1], th.data(), K, opt.jobs, nullptr);
    int best_epoch = 0, steps = 0;
    std::printf("epoch 0: train %.6f valid %.6f\n", model_loss(split[0], th.data(), K, opt.jobs, nullptr), best_valid);
    if(opt.method=="adam")
    {
        // full-batch Adam
        std::vector<double> grad(n_params), m(n_params, 0), v(n_params, 0);
        const double b1 = 0.9, b2 = 0.999;
        for(int ep=1; ep<=opt.epochs; ep++)
        {
            const double train = model_loss(split[0], th.data(), K, opt.jobs, grad.data());
            const double lr = opt.lr*(0.01 + 0.99*0.5*(1+std::cos(M_PI*(ep-1)/opt.epochs)));
            for(int j=0;j<n_params;j++)
            {
                if(!param_tuned[j]) continue;
                const double gj = grad[j] + 2*opt.lambda*(th[j]-th0[j]);
                m[j] = b1*m[j] + (1-b1)*gj;
                v[j] = b2*v[j] + (1-b2)*gj*gj;
                const double mh = m[j]/(1-std::pow(b1, ep)), vh = v[j]/(1-std::pow(b2, ep));
                if(vh>0) th[j] -= lr*mh/(std::sqrt(vh)+1e-12);
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
        // Levenberg-Marquardt: (A + mu diag A) step = -g, kept if the objective falls
        std::vector<double> A, g, U, step(nt), tn;
        double F = model_loss(split[0], th.data(), K, opt.jobs, nullptr) + regular(th.data());
        double mu = opt.mu;
        for(int it=1; it<=opt.iters; it++)
        {
            normal_eqs(split[0], th.data(), th0.data(), K, opt.lambda, opt.jobs, A, g);
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
                U = A;
                for(int t=0;t<nt;t++) U[(size_t)t*nt+t] *= 1+mu;
                if(!cholesky(U, nt)) { mu *= 10; continue; }
                for(int t=0;t<nt;t++) step[t] = -g[t];
                cholesky_solve(U, nt, step.data());
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

    // standard errors at the kept point: sigma^2/n (J'J/n + lambda I)^-1, and again with
    // 10 lambda, to tell what the data holds from what lambda holds
    std::vector<double> se(nt, 0), se_c(nt, 0);
    std::vector<int> se_flag(nt, 0);    // 1: no data, 2: held by lambda
    std::vector<std::vector<std::pair<int,double>>> contrast(nt);
    double sigma2 = 0;
    bool ridged = false;
    if(opt.se)
    {
        std::vector<double> A, g;
        sigma2 = normal_eqs(split[0], best.data(), th0.data(), K, opt.lambda, opt.jobs, A, g);
        const double n = split[0].pos.size();
        std::vector<double> S[2];
        for(int pass=0;pass<2;pass++)
        {
            std::vector<double> U = A;
            for(int t=0;t<nt;t++) U[(size_t)t*nt+t] += pass*9*opt.lambda;
            if(!cholesky(U, nt))
            {
                U = A;
                double top = 0;
                for(int t=0;t<nt;t++) top = std::max(top, A[(size_t)t*nt+t]);
                for(int t=0;t<nt;t++) U[(size_t)t*nt+t] += pass*9*opt.lambda + 1e-12*top;
                ridged = true;
                if(!cholesky(U, nt)) { std::fprintf(stderr, "the normal equations are not positive definite\n"); return 1; }
            }
            S[pass] = cholesky_inverse(U, nt);
            for(double& x : S[pass]) x *= sigma2/n;
        }
        // gauge-free contrasts: a table or mobility entry against its table's mean, a piece
        // value plus the means of its tables (adding c to a table and -c to the value leaves
        // the eval alone)
        auto data = [&](int t) { return A[(size_t)t*nt+t] > opt.lambda*(1+1e-6); };
        auto members = [&](int j0, int count) { std::vector<int> v; for(int k=0;k<count;k++) if(tix[j0+k]>=0 && data(tix[j0+k])) v.push_back(tix[j0+k]); return v; };
        for(int t=0;t<nt;t++) contrast[t] = {{t, 1.0}};
        auto against_mean = [&](const std::vector<int>& T)
        {
            for(int t : T) for(int u : T) contrast[t].push_back({u, -1.0/T.size()});
        };
        // piece order: pawn rook knight bishop queen king
        const int* mob[5][2] = {{nullptr, nullptr}, {W_FROM.mobility_rook_opening, W_FROM.mobility_rook_endgame},
                                {W_FROM.mobility_knight_opening, W_FROM.mobility_knight_endgame},
                                {W_FROM.mobility_bishop_opening, W_FROM.mobility_bishop_endgame}, {W_FROM.mobility_queen_opening, W_FROM.mobility_queen_endgame}};
        const int mob_n[5] = {0, 15, 9, 14, 28};
        for(int piece=0;piece<6;piece++)
        for(int ph=0;ph<2;ph++)
        {
            const std::vector<int> T = members((ph ? TE_BASE : TO_BASE) + 64*piece, 64);
            const std::vector<int> M = piece>=1 && piece<=4 ? members(flat(W_FROM, mob[piece][ph]), mob_n[piece]) : std::vector<int>();
            const int v = tix[flat(W_FROM, ph ? &W_FROM.piece_value_endgame[piece] : &W_FROM.piece_value[piece])];
            if(v>=0)
            {
                for(int u : T) contrast[v].push_back({u, 1.0/T.size()});
                for(int u : M) contrast[v].push_back({u, 1.0/M.size()});
            }
            against_mean(T);
            against_mean(M);
        }
        auto var = [&](const std::vector<double>& Sp, int t)
        {
            double s = 0;
            for(auto [a, ca] : contrast[t]) for(auto [b, cb] : contrast[t]) s += ca*cb*Sp[(size_t)a*nt+b];
            return s;
        };
        for(int t=0;t<nt;t++)
        {
            se[t] = std::sqrt(S[0][(size_t)t*nt+t]);
            se_c[t] = std::sqrt(var(S[0], t));
            if(!data(t)) se_flag[t] = 1;
            else if(se_c[t] > 1.5*std::sqrt(var(S[1], t))) se_flag[t] = 2;
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
        auto value = [&](WEIGHTS& W, int t) { double x = 0; for(auto [u, c] : contrast[t]) x += c*at(W, jof[u]); return x; };
        auto group = [&](int j) { return param_name[j].substr(0, param_name[j].find('[')); };
        say(report, "standard errors (* = gauge-free: a table or mobility entry against its table's mean,");
        say(report, "a piece value plus those means), sigma^2 %.6f%s:", sigma2, ridged ? ", ridge 1e-12 * max diag added" : "");
        say(report, "  %-34s %5s %8s %8s %6s%s", "group", "n", "median", "median*", "lambda", cmp ? "  >2 SE* vs compare" : "");
        std::vector<std::string> order;
        for(int t=0;t<nt;t++) if(std::find(order.begin(), order.end(), group(jof[t]))==order.end()) order.push_back(group(jof[t]));
        int total = 0, beyond2 = 0, beyond3 = 0, held = 0, nodata = 0;
        for(const std::string& gname : order)
        {
            std::vector<double> a, b;
            int n_lambda = 0, n_beyond = 0;
            for(int t=0;t<nt;t++)
            {
                if(group(jof[t])!=gname) continue;
                if(se_flag[t]==1) { nodata++; continue; }
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
        say(report, "lambda: SE* shrinks by more than 1.5 with 10 lambda (%d weights); %d weights have no data", held, nodata);
        if(cmp)
        say(report, "set %d vs set %d: %d of %d gauge-free differences exceed 2 SE*, %d exceed 3", W_CMP.version, W_NEW.version, beyond2, total, beyond3);
        se_lines = "\n# standard errors: weight, value, SE, SE* (see the report), flag\n";
        for(int t=0;t<nt;t++)
        {
            std::snprintf(line, sizeof line, "# se %-34s %6d %8.2f %8.2f%s\n", param_name[jof[t]].c_str(), at(W_NEW, jof[t]), se[t], se_c[t],
                          se_flag[t]==1 ? " no-data" : se_flag[t]==2 ? " lambda" : "");
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
