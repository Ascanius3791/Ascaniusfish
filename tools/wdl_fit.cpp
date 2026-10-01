// OWNERSHIP=Claude
// tools/wdl_fit: fits Stockfish's win/draw/loss model to our own games (#54).
//
//   ./tools/wdl_fit <game.pgn> [more.pgn ...] [boot=200] [seed=54]
//
// The model, x the score in cp from the mover's view:
//   P(win) = σ((x − o)/s),  P(loss) = σ((−x − o)/s),  P(draw) = the rest,
//   E(x) = P(win) + P(draw)/2 = ½·(1 + σ((x − o)/s) − σ((−x − o)/s)).
// o and s are fitted by maximum likelihood over every (score, result) pair of
// the games in tools/match's PGN: each move's comment "{+0.35/7 1.21s}" is
// the mover's score, and the game's result is read from the mover's side.
// Moves without a score (book moves) and mate or tablebase scores ("M",
// |cp| ≥ 10000) are left out.
//
// The pairs of one game are far from independent, so the interval is a
// bootstrap over games (`boot` resamples), not the likelihood's curvature.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

constexpr int TB_CP = 10000;   // uci_score() shows a tablebase win as cp ±10000

struct Pair { float x; int r; };   // r: +1 the mover won, 0 a draw, −1 the mover lost
struct Game_Pairs { std::vector<Pair> pairs; };

struct Totals { long moves = 0, used = 0, unscored = 0, mates = 0, tb = 0; int games = 0, w = 0, d = 0, l = 0; };

static double log_sigmoid(double z) { return z >= 0 ? -std::log1p(std::exp(-z)) : z - std::log1p(std::exp(z)); }

// The pairs with one score, counted by result: n[0] lost, n[1] drawn, n[2] won.
// Scores are whole cp, so ~50k pairs are a few thousand of these.
struct Score_Counts { double x; double n[3]; };

static std::vector<Score_Counts> count(const std::vector<const Pair*>& pairs)
{
    std::map<int, Score_Counts> by_score;
    for(const Pair* p : pairs)
    {
        Score_Counts& c = by_score.try_emplace(int(p->x), Score_Counts{p->x, {0, 0, 0}}).first->second;
        c.n[p->r + 1]++;
    }
    std::vector<Score_Counts> out;
    for(const auto& [x, c] : by_score) out.push_back(c);
    return out;
}

// Mean log-likelihood of the counted pairs under (o, s).
static double log_likelihood(const std::vector<Score_Counts>& counts, double o, double s)
{
    double sum = 0, n = 0;
    for(const Score_Counts& c : counts)
    {
        const double a = (c.x - o) / s, b = (-c.x - o) / s;
        const double draw = 1 - 1/(1 + std::exp(-a)) - 1/(1 + std::exp(-b));
        sum += c.n[2] * log_sigmoid(a) + c.n[0] * log_sigmoid(b);
        if(c.n[1] > 0) sum += c.n[1] * std::log(std::max(draw, 1e-300));
        n += c.n[0] + c.n[1] + c.n[2];
    }
    return sum / n;
}

// The maximum of the likelihood: a grid over o and log s, refined around its best point.
static void fit(const std::vector<const Pair*>& pairs, double& o_best, double& s_best, double& ll_best)
{
    const std::vector<Score_Counts> counts = count(pairs);
    double o_lo = 1, o_hi = 1000, ls_lo = std::log(10.0), ls_hi = std::log(2000.0);
    ll_best = -INFINITY;
    for(int round = 0; round < 6; round++)
    {
        constexpr int N = 24;
        double bo = o_best, bls = std::log(s_best > 0 ? s_best : 100);
        for(int i = 0; i <= N; i++)
        for(int j = 0; j <= N; j++)
        {
            const double o = o_lo + (o_hi - o_lo) * i / N, ls = ls_lo + (ls_hi - ls_lo) * j / N;
            const double ll = log_likelihood(counts, o, std::exp(ls));
            if(ll > ll_best) { ll_best = ll; bo = o; bls = ls; }
        }
        const double ro = (o_hi - o_lo) * 2 / N, rls = (ls_hi - ls_lo) * 2 / N;
        o_lo = std::max(0.01, bo - ro); o_hi = bo + ro;
        ls_lo = bls - rls; ls_hi = bls + rls;
        o_best = bo; s_best = std::exp(bls);
    }
}

// One PGN file's games; false if it cannot be read or a game does not add up.
static bool read_pgn(const std::string& path, std::vector<Game_Pairs>& games, Totals& t)
{
    std::ifstream in(path);
    if(!in) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return false; }
    std::string line, result, fen, text;
    int plies = -1;
    auto finish = [&]() -> bool
    {
        if(result.empty()) return true;
        std::vector<std::string> comments;
        for(size_t a = text.find('{'); a != std::string::npos; a = text.find('{', a + 1))
        comments.push_back(text.substr(a + 1, text.find('}', a) - a - 1));
        if(comments.empty() || int(comments.size()) - 1 != plies)
        {
            std::fprintf(stderr, "%s: a game has %zu comments for %d plies\n", path.c_str(), comments.size(), plies);
            return false;
        }
        const int white_r = result == "1-0" ? 1 : result == "0-1" ? -1 : 0;
        (white_r > 0 ? t.w : white_r < 0 ? t.l : t.d)++;
        t.games++;
        bool white = fen.empty() || fen.find(" w ") != std::string::npos;
        Game_Pairs g;
        for(int i = 0; i < plies; i++, white = !white)
        {
            t.moves++;
            const std::string& c = comments[i];
            const size_t slash = c.find('/');
            if(slash == std::string::npos) { t.unscored++; continue; }
            if(c.find('M') < slash) { t.mates++; continue; }
            const int cp = int(std::lround(std::atof(c.c_str()) * 100));
            if(std::abs(cp) >= TB_CP) { t.tb++; continue; }
            g.pairs.push_back({float(cp), white ? white_r : -white_r});
            t.used++;
        }
        games.push_back(std::move(g));
        result.clear(); fen.clear(); text.clear(); plies = -1;
        return true;
    };
    while(std::getline(in, line))
    {
        if(line.rfind("[Result \"", 0) == 0) { if(!finish()) return false; result = line.substr(9, line.find('"', 9) - 9); }
        else if(line.rfind("[FEN \"", 0) == 0) fen = line.substr(6, line.find('"', 6) - 6);
        else if(line.rfind("[PlyCount \"", 0) == 0) plies = std::atoi(line.c_str() + 11);
        else if(!line.empty() && line[0] != '[') text += line + " ";
    }
    return finish();
}

int main(int argc, char** argv)
{
    std::vector<std::string> files;
    int boot = 200, seed = 54;
    for(int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        if(a.rfind("boot=", 0) == 0) boot = std::atoi(a.c_str() + 5);
        else if(a.rfind("seed=", 0) == 0) seed = std::atoi(a.c_str() + 5);
        else files.push_back(a);
    }
    if(files.empty()) { std::fprintf(stderr, "usage: %s <game.pgn> [more.pgn ...] [boot=200] [seed=54]\n", argv[0]); return 1; }

    std::vector<Game_Pairs> games;
    Totals t;
    for(const std::string& f : files)
    if(!read_pgn(f, games, t)) return 1;

    std::vector<const Pair*> all;
    for(const Game_Pairs& g : games)
    for(const Pair& p : g.pairs) all.push_back(&p);
    std::printf("wdl_fit: %d games (white W/D/L %d/%d/%d), %ld moves: %ld used, %ld unscored, %ld mate, %ld tablebase\n",
                t.games, t.w, t.d, t.l, t.moves, t.used, t.unscored, t.mates, t.tb);

    double o = 0, s = 0, ll = 0;
    fit(all, o, s, ll);
    std::printf("fit: o = %.1f cp, s = %.1f cp, mean log-likelihood %.5f\n", o, s, ll);

    std::mt19937_64 rng(seed);
    std::vector<double> os, ss;
    for(int b = 0; b < boot; b++)
    {
        std::vector<const Pair*> sample;
        for(size_t k = 0; k < games.size(); k++)
        for(const Pair& p : games[rng() % games.size()].pairs) sample.push_back(&p);
        double bo = o, bs = s, bll = 0;
        fit(sample, bo, bs, bll);
        os.push_back(bo); ss.push_back(bs);
    }
    if(boot > 0)
    {
        std::sort(os.begin(), os.end());
        std::sort(ss.begin(), ss.end());
        const size_t lo = size_t(0.025 * boot), hi = std::min(size_t(boot) - 1, size_t(0.975 * boot));
        std::printf("bootstrap over games (%d): o 95%% [%.1f, %.1f], s 95%% [%.1f, %.1f]\n", boot, os[lo], os[hi], ss[lo], ss[hi]);
    }

    // The fitted curve against the games, in bins of the score.
    std::printf("\n    score bin    pairs   win%%  draw%%  loss%%   E(games)  E(fit)\n");
    const int edges[] = {-100000, -800, -400, -200, -100, -50, -20, 20, 50, 100, 200, 400, 800, 100000};
    for(size_t k = 0; k + 1 < std::size(edges); k++)
    {
        long n = 0, w = 0, d = 0;
        double e_fit = 0;
        for(const Pair* p : all)
        if(p->x >= edges[k] && p->x < edges[k+1])
        {
            n++; w += p->r > 0; d += p->r == 0;
            e_fit += 0.5 * (1 + 1/(1 + std::exp(-(p->x - o)/s)) - 1/(1 + std::exp(-(-p->x - o)/s)));
        }
        if(!n) continue;
        std::printf("  [%6d,%6d) %7ld  %5.1f  %5.1f  %5.1f   %7.3f  %7.3f\n", edges[k], edges[k+1], n,
                    100.0*w/n, 100.0*d/n, 100.0*(n-w-d)/n, (w + 0.5*d)/n, e_fit/n);
    }
    return 0;
}
