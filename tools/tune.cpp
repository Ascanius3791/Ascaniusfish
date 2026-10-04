// OWNERSHIP=Claude
// tools/tune: fits a weight set to game results (issue #87, docs/TUNING_PLAN.md "Method").
//
//   ./tools/tune [data=data/tune] [from=weights/w1.txt] [out=weights/w<from+1>.txt]
//                [jobs=6] [epochs=1500] [lr=2] [lambda=1e-9] [k=0] [tag=ccrl4040-3000-balanced]
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
// positional_eval(), passed_pawn_eval(), piece_activity_eval(), tempo_eval(),
// king_safety_detail()) with one weight set to a probe value and the rest of that
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
// (docs/measurements/tune_w2_2026-10-04.md).
//
// Full-batch Adam, the learning rate falling from lr to lr/100 (cosine); the epoch
// with the lowest valid loss of the model is kept. The weights are rounded, and the
// report gives train/valid/test loss of both sets by the real basic_eval(). It is
// printed and written as '#' lines at the end of the new set.
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

// Feature words. A table piece: bits 0-8 piece*64 + square (white's view), bit 9 set
// for black, bits 16-23 the game phase (0..PHASE_MAX, the endgame weight is
// PHASE_MAX - phase). A linear or danger feature: bits 0-11 the parameter, bits 16-31
// the numerator (int16).
struct Pos
{
    uint32_t off;               // first feature word
    uint8_t n_tab, n_lin, n_ks[2];   // n_ks[1]: white king's danger units (gated in)
    uint8_t r2;                 // 2 * result
};

struct Split
{
    std::vector<Pos> pos;
    std::vector<uint32_t> words;
    std::vector<int> base;      // basic_eval() with the from set
    std::vector<std::string> fens;   // only while the features are made
};

static WEIGHTS W_FROM;
static int TO_BASE, TE_BASE;            // flat index of the tables' [0][0]

struct Probe_Group { std::vector<int> params; int value; };
static Probe_Group G_MATERIAL, G_POSITIONAL, G_PASSED, G_ACTIVITY, G_TEMPO, G_KS_ATTACK, G_KS_SHELTER;

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
    p.off = words.size();
    p.r2 = (uint8_t)std::lround(2*result);
    // piecetable(): both sides by the one game phase
    const int phase = game_phase(&b);
    for(int piece=0;piece<12;piece++)
    {
        uint64_t bb = b.Board[piece];
        while(bb)
        {
            const int sq = find_and_delete_trailling_1(bb);
            const bool black = piece>=6;
            words.push_back((uint32_t)((piece%6)*64 + (black ? sq^56 : sq)) | (black ? 1u<<9 : 0) | (uint32_t)phase << 16);
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
    for(int j : touched)
    if(acc[j]) { words.push_back(lin_word(j, acc[j])); p.n_lin++; }
    for(int side=1;side>=0;side--)
    {
        words.insert(words.end(), ks[side].begin(), ks[side].end());
        p.n_ks[side] = ks[side].size();
    }
    return p;
}

// ---- the model

// The model's eval of p at theta; with grad, adds g * d eval / d theta to grad.
static inline double model_eval(const Pos& p, const uint32_t* words, const double* th, double* grad, double g)
{
    const uint32_t* w = words + p.off;
    double e = 0;
    // tables
    double tab = 0;
    for(int i=0;i<p.n_tab;i++, w++)
    {
        const int idx = *w & 511, ph = *w >> 16 & 255;
        const double sign = *w & 512 ? -1.0 : 1.0;
        tab += sign*(th[TO_BASE+idx]*ph + th[TE_BASE+idx]*(PHASE_MAX-ph));
    }
    e += tab/PHASE_MAX;
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
        w = words + p.off;
        for(int i=0;i<p.n_tab;i++, w++)
        {
            const int idx = *w & 511, ph = *w >> 16 & 255;
            const double gs = (*w & 512 ? -g : g)/PHASE_MAX;
            grad[TO_BASE+idx] += gs*ph;
            grad[TE_BASE+idx] += gs*(PHASE_MAX-ph);
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
            const double e = model_eval(p, s.words.data(), th, nullptr, 0);
            const double q = sigmoid(e/K);
            sum += (q-r)*(q-r);
            if(gt) model_eval(p, s.words.data(), th, gt, 2*(q-r)*q*(1-q)/K);
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
        s.fens.push_back(line.substr(b+1, c-b-1));
        results.push_back(std::atof(line.substr(e+1, f-e-1).c_str()));
    }
    s.pos.assign(s.fens.size(), Pos{});
    s.base.assign(s.fens.size(), 0);
    for(size_t i=0;i<results.size();i++) s.pos[i].r2 = (uint8_t)std::lround(2*results[i]);
    return !s.fens.empty();
}

// Features of every position of s, in jobs threads.
static void make_features(Split& s, int jobs)
{
    const size_t n = s.fens.size();
    std::vector<std::vector<uint32_t>> words(jobs);
    std::vector<std::thread> pool;
    for(int t=0;t<jobs;t++)
    pool.emplace_back([&, t]
    {
        for(size_t i=n*t/jobs;i<n*(t+1)/jobs;i++)
        {
            BB b;
            if(!uci_parse_fen(s.fens[i], b)) { std::fprintf(stderr, "bad FEN %s\n", s.fens[i].c_str()); std::exit(1); }
            const double r = s.pos[i].r2*0.5;
            s.pos[i] = make_pos(b, r, words[t]);
            s.base[i] = basic_eval(&b, W_FROM);
        }
    });
    for(auto& th : pool) th.join();
    size_t off = 0;
    for(int t=0;t<jobs;t++)
    {
        for(size_t i=n*t/jobs;i<n*(t+1)/jobs;i++) s.pos[i].off += off;
        off += words[t].size();
    }
    s.words.reserve(off);
    for(int t=0;t<jobs;t++) { s.words.insert(s.words.end(), words[t].begin(), words[t].end()); std::vector<uint32_t>().swap(words[t]); }
}

// Mean loss of the real basic_eval() with W over s.
static double real_loss(const Split& s, const WEIGHTS& W, double K, int jobs)
{
    const size_t n = s.fens.size();
    std::vector<double> part(jobs, 0);
    std::vector<std::thread> pool;
    for(int t=0;t<jobs;t++)
    pool.emplace_back([&, t]
    {
        double sum = 0;
        for(size_t i=n*t/jobs;i<n*(t+1)/jobs;i++)
        {
            BB b;
            uci_parse_fen(s.fens[i], b);
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
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }

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
        make_features(split[s], opt.jobs);
        std::printf("%s: %zu positions, %.1f features each (%.1f s)\n", names[s], split[s].pos.size(),
                    (double)split[s].words.size()/split[s].pos.size(), secs());
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
            const double diff = std::fabs(model_eval(s.pos[i], s.words.data(), th0.data(), nullptr, 0) - s.base[i]);
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

    // full-batch Adam
    std::vector<double> th = th0, best = th0, grad(n_params), m(n_params, 0), v(n_params, 0);
    double best_valid = model_loss(split[1], th.data(), K, opt.jobs, nullptr);
    int best_epoch = 0;
    std::printf("epoch 0: train %.6f valid %.6f\n", model_loss(split[0], th.data(), K, opt.jobs, nullptr), best_valid);
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

    // the new set: rounded, then judged by the real eval
    WEIGHTS W_NEW = W_FROM;
    for(int j=0;j<n_params;j++)
    if(param_tuned[j]) at(W_NEW, j) = (int)std::lround(best[j]);
    W_NEW.version = W_FROM.version+1;
    double loss[2][3];
    for(int s=0;s<3;s++)
    {
        loss[0][s] = base_loss(split[s], K);
        loss[1][s] = real_loss(split[s], W_NEW, K, opt.jobs);
    }

    char line[512];
    std::string report;
    say(report, "tools/tune %s: set %d from set %d, %s", ENGINE_COMMIT, W_NEW.version, W_FROM.version, opt.data.c_str());
    say(report, "positions: train %zu, valid %zu, test %zu", split[0].pos.size(), split[1].pos.size(), split[2].pos.size());
    say(report, "K %.2f (fitted on set %d), lambda %g, lr %g, epochs %d, best epoch %d (valid, model %.6f), %.0f s",
        K, W_FROM.version, opt.lambda, opt.lr, opt.epochs, best_epoch, best_valid, secs());
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
    std::ofstream out(opt.out);
    out << text;
    if(!out) { std::fprintf(stderr, "cannot write %s\n", opt.out.c_str()); return 1; }
    std::printf("wrote %s\n", opt.out.c_str());
    return 0;
}
