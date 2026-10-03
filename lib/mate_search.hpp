// OWNERSHIP=Claude
#ifndef MATE_SEARCH_HPP
#define MATE_SEARCH_HPP
#include "move_generation.hpp"
#include "search_control.hpp"
#include "syzygy.hpp"
#include "gaviota.hpp"
#include "tb_search.hpp"
#include <cstdint>
#include <climits>
#include <vector>

// The mate search (issue #78): a search of its own, not minimax(), that asks
// only "does the attacker mate within r plies?". That is the null window at
// the mate score INT_MAX - r (white mating; INT_MIN + r for black), so there
// is no eval anywhere: a node is a mate found, or not within r.
//
// No null move, LMR, LMP, quiescence or extensions, and the defender's moves
// are never pruned, so every mate it finds is real. Two modes:
// - FULL_WIDTH: the attacker tries every move. Complete: "no mate within r"
//   is proven, so deepening r upward makes the first hit the quickest mate.
// - CHECKS_ONLY: the attacker plays only checks (every evasion when in
//   check). Fast and sound, not complete: a miss proves nothing about mates
//   that need a quiet move.
// The 50-move rule and repetitions are ignored.
//
// Ordering: the attacker's moves by the fewest flight squares they leave the
// defending king (checks first), the defender's by the most it keeps; the TT
// move first. With the Syzygy tables, a child with <= SyzygyProbeLimit pieces
// that is not a win for the attacker (a cursed win counts: no 50-move rule
// here) can never be mated and is cut; with the Gaviota tables, such a child
// gives its exact distance to mate.

enum class Mate_Mode : uint8_t { FULL_WIDTH, CHECKS_ONLY };

// Thrown when a search spends its node budget; whatever the TT holds by then
// is proven, so a later search with a new budget goes on from there.
struct mate_budget_exhausted {};

struct Mate_Context
{
    bool attacker_white = true;
    Mate_Mode mode = Mate_Mode::FULL_WIDTH;
    long long nodes = 0;
    long long budget = LLONG_MAX;
};

// One node: a mate for ctx.attacker_white within `plies` plies of pos? Returns
// the length of the mate it proved (<= plies; not always the quickest, see
// find_mate()), or -1 for none within `plies` (proven in FULL_WIDTH). wfh is BB
// scratch, one per ply. Throws mate_budget_exhausted, and search_aborted via
// poll_search_abort() like minimax() (every node counts in search_nodes).
int minimax_checkmate_only(const BB* const pos, BB* const wfh, int plies, Mate_Context& ctx);

struct Mate_Result
{
    enum Status { FOUND, NONE, UNKNOWN };
    Status status = UNKNOWN;
    int plies = 0;           // FOUND: the mate's length. NONE/UNKNOWN: no mate within this many proven (FULL_WIDTH)
    bool quickest = false;   // FOUND by FULL_WIDTH with every shorter length ruled out
    std::vector<Move> line;  // FOUND: the mating line from the TT (may stop short if an entry was overwritten)
    long long nodes = 0;
};

// Deepens r upward (1 or 0, as who is to move, then +2 each time) up to
// max_plies, so the first hit is the quickest mate the mode can see. FULL_WIDTH
// up to `full_proven` (no mate within it already proven; -1 = nothing) is skipped. Throws
// search_aborted only; the budget ends it as UNKNOWN.
Mate_Result find_mate(const BB& pos, BB* const wfh, bool attacker_white, int max_plies, Mate_Mode mode,
                      long long node_budget, int full_proven = -1);

// Nodes the quiescence search's mate check may spend (minimax_tactical()),
// and how many plies past the claim it looks.
constexpr long long QUIESCENCE_MATE_BUDGET = 20000;
constexpr int QUIESCENCE_MATE_EXTRA_PLIES = 10;

// Does the mate `eval` (white's view, plies from pos, as the search scores
// it) hold against every defence? A CHECKS_ONLY search deepened from the
// claimed plies up to QUIESCENCE_MATE_EXTRA_PLIES more, within `budget` nodes;
// on success `eval` becomes the mate it proved (it can be longer than the
// claim: a defence the claim skipped may hold out longer), false if it finds
// none or runs out. Throws search_aborted only. For minimax_tactical(), out of
// check budget, where a skipped evasion may yet escape or last longer (#78).
bool mate_confirmed(const BB* const pos, BB* const wfh, int& eval, long long budget = QUIESCENCE_MATE_BUDGET);

// mate_confirmed() calls since the process started, and how many held.
inline long long mate_checks = 0, mate_checks_confirmed = 0;

// The mating line of a mate within `plies` just proven in `mode`: the
// attacker's quickest move the TT knows, the defence that lasts longest. A
// child the TT has lost is searched again; the line stops short if that runs
// out of its budget.
std::vector<Move> mate_line(const BB& pos, BB* const wfh, bool attacker_white, int plies, Mate_Mode mode);

// The mate TT: 2^MATE_TT_BITS entries of 16 bytes (8 MB), allocated on the
// first search. Small on purpose: several engines run at once (see
// lib/Settings.hpp's TT note).
#ifndef MATE_TT_BITS
#define MATE_TT_BITS 19
#endif
void mate_tt_clear();

#include "../src/mate_search.cpp"

#endif // MATE_SEARCH_HPP
