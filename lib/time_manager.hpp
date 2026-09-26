// OWNERSHIP=Claude
#ifndef TIME_MANAGER_HPP
#define TIME_MANAGER_HPP

#include "move_generation.hpp"

// Time management for x+y clocks (issue #6).
//
// A move may take y + T, with y the increment and T = λ·u. u is an equal share
// of the remaining clock, u = time / expected_moves_left(). λ in [0,1] says how
// unsettled the search still is. It is recomputed after every finished
// iteration from the principal variations of all iterations so far:
//
//   pair (d, d+1):  λ_pair = 1 − Σ_{k=1..l} c_k·2^−k
//                   c_k = 1 if move k of both PVs is the same, l = shorter PV length
//   λ = Σ w·λ_pair / Σ w,  w = 1/(D − (d+1) + 1),  D = deepest finished iteration
//
// So equal long PVs give λ ≈ 0, short or changing PVs give λ ≈ 1, and the
// newest pairs weigh most. y + λ·u is the soft limit: the next iteration is
// only started if it is expected to finish within it. The hard limit
// (search_deadline_ns) doesn't depend on λ and aborts a running iteration.
//
// TM_PV_STABILITY=1 turns λ on. Off by default: as specified, λ ≤ 1 only
// ever shortens the think time, and at 60+0.6 that lost -161 ± 100 Elo (#6).
#ifndef TM_PV_STABILITY
#define TM_PV_STABILITY 0
#endif

constexpr int TM_MAX_DEPTH = 64;              // >= UCI_MAX_DEPTH
constexpr int TM_PV_LEN = 24;                 // PV moves compared per pair; 2^-24 is negligible
constexpr int TM_DEFAULT_MOVES_LEFT = 40;
constexpr long long TM_MOVE_OVERHEAD_MS = 50; // abort polling granularity + pipe latency
constexpr long long TM_HARD_SHARES = 4;       // hard limit = y + 4u (capped by the clock)
// Next iteration ≈ 4× the last one: the median over the bench positions. The
// ratio of the last two iterations predicts it no better (TT, odd/even depths).
constexpr long long TM_ITERATION_GROWTH = 4;

// Moves per side the game is expected to last from `pos`. Constant for now (issue #7).
int expected_moves_left(const BB& pos);

// λ from the PVs of iterations 1..deepest: pv[d] holds len[d] moves of iteration d.
// 1 if fewer than two iterations are known.
double pv_instability(const Move pv[][TM_PV_LEN], const int* len, int deepest);

class TimeManager
{
    public:
    // time_ms/inc_ms: clock and increment of the side to move; movestogo 0 = none.
    TimeManager(const BB& root, long long time_ms, long long inc_ms, int movestogo);

    long long hard_ms() const { return hard; }
    long long soft_ms() const;
    double lambda() const { return lam; }

    // Call after iteration `depth` has finished, `elapsed_ms` after the search started.
    void iteration_done(int depth, const PV_Line& pv, long long elapsed_ms);

    // Whether the next iteration is expected to finish within the soft limit.
    bool start_next_iteration(long long elapsed_ms) const;

    private:
    long long inc, share, hard;  // y, u, hard limit (ms)
    double lam = 1;
    int deepest = 0;
    Move pv[TM_MAX_DEPTH+1][TM_PV_LEN];
    int len[TM_MAX_DEPTH+1];
    long long finished_at[TM_MAX_DEPTH+1];  // elapsed ms when iteration d finished
};

#include "../src/time_manager.cpp"

#endif // TIME_MANAGER_HPP
