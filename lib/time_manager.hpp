// OWNERSHIP=Claude
#ifndef TIME_MANAGER_HPP
#define TIME_MANAGER_HPP

#include "move_generation.hpp"
#include "basic_eval.hpp"  // enemy_material_left_percent(), for expected_moves_left()
#include "eval.hpp"        // eval(), for expected_moves_left()

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
// The first attempt used this raw λ directly and lost -161 ± 100 Elo at
// 60+0.6 (#6): raw λ is 1 only when consecutive iterations disagree on the
// whole PV, so most moves score well under 1 and the game as a whole
// systematically underspends its share u, not just the settled positions λ
// was meant to shorten. A second attempt normalized λ against its own
// recent past (Lambda_History below: λ_avg is a weighted average of the raw
// λ chosen for the last TM_LAMBDA_HISTORY moves, and the λ actually used is
// raw_λ / λ_avg, clamped to TM_HARD_SHARES) — still off by default, since a
// 10-game test at 60+1 lost -147 ± 148 Elo, about the same size as the
// original failure. Leading hypothesis: raw λ likely drifts upward as a
// game gets more tactical, so a trailing average of *earlier, quieter* moves
// underestimates a later move's raw λ, pushing the correction above 1 more
// often than below as the game goes on — net overspending in the
// middlegame that leaves the engine short later. Not confirmed; untangling
// that from expected_moves_left()'s own share-of-remaining-time assumption
// is unfinished work. TM_PV_STABILITY=1 re-enables it for testing.
#ifndef TM_PV_STABILITY
#define TM_PV_STABILITY 0
#endif

constexpr int TM_MAX_DEPTH = 64;              // >= UCI_MAX_DEPTH
constexpr int TM_PV_LEN = 24;                 // PV moves compared per pair; 2^-24 is negligible
constexpr int TM_MIN_MOVES_LEFT = 5;          // floor for expected_moves_left(), so one wild eval swing can't blow up a move's share
constexpr long long TM_MOVE_OVERHEAD_MS = 50; // abort polling granularity + pipe latency
constexpr long long TM_HARD_SHARES = 4;       // hard limit = y + 4u (capped by the clock)
// Next iteration ≈ 4× the last one: the median over the bench positions. The
// ratio of the last two iterations predicts it no better (TT, odd/even depths).
constexpr long long TM_ITERATION_GROWTH = 4;
constexpr int TM_LAMBDA_HISTORY = 10;  // moves of raw λ kept to normalize the next one; older ones aren't significant

// Moves per side the game is expected to last from `pos` (issue #7): 20,
// plus up to 20 more for how much material is still on the board (both
// sides' fraction of their own starting army, averaged), minus how decisive
// the position already looks (|eval|/200 centipawns). A full board near
// equal (game start) gives 40 — same as the old flat constant it replaces —
// while a stripped-down or already-decided position gives less, so its
// share of the remaining clock isn't diluted by moves that are unlikely to
// be played. Floored at TM_MIN_MOVES_LEFT.
int expected_moves_left(const BB& pos);

// λ from the PVs of iterations 1..deepest: pv[d] holds len[d] moves of iteration d.
// 1 if fewer than two iterations are known.
double pv_instability(const Move pv[][TM_PV_LEN], const int* len, int deepest);

// Raw λ chosen for each of the last up to TM_LAMBDA_HISTORY moves, most
// recent first, so TimeManager can normalize the next move's λ against them.
// One instance per game; reset with the game (ucinewgame).
class Lambda_History
{
    public:
    void push(double raw_lambda);
    // Weighted average (1/2, 1/4, ... over whatever is there, normalized by
    // their sum), or 1 (neutral, no correction) with no history yet.
    double average() const;
    void reset() { count = 0; }

    private:
    double values[TM_LAMBDA_HISTORY];  // values[0] = most recent
    int count = 0;
};

class TimeManager
{
    public:
    // time_ms/inc_ms: clock and increment of the side to move; movestogo 0 = none.
    // history: the game's Lambda_History, shared across moves; must outlive this TimeManager.
    TimeManager(const BB& root, long long time_ms, long long inc_ms, int movestogo, Lambda_History& history);

    long long hard_ms() const { return hard; }
    long long soft_ms() const;
    double lambda() const { return lam; }
    double raw_lambda() const { return raw_lam; }

    // Call after iteration `depth` has finished, `elapsed_ms` after the search started.
    void iteration_done(int depth, const PV_Line& pv, long long elapsed_ms);

    // Whether the next iteration is expected to finish within the soft limit.
    bool start_next_iteration(long long elapsed_ms) const;

    // Files this move's raw λ into the shared history once it's decided, so
    // the next move's TimeManager normalizes against it too. Call once,
    // after the last iteration_done() for this move.
    void commit_lambda() { history.push(raw_lam); }

    private:
    Lambda_History& history;
    long long inc, share, hard;  // y, u, hard limit (ms)
    double lam = 1, raw_lam = 1;
    int deepest = 0;
    Move pv[TM_MAX_DEPTH+1][TM_PV_LEN];
    int len[TM_MAX_DEPTH+1];
    long long finished_at[TM_MAX_DEPTH+1];  // elapsed ms when iteration d finished
};

#include "../src/time_manager.cpp"

#endif // TIME_MANAGER_HPP
