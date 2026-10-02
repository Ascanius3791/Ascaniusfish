// OWNERSHIP=Claude
#ifndef PLAN_EVAL_HPP
#define PLAN_EVAL_HPP
#include "basic_eval.hpp"
#include "nne.hpp"

// Plan eval, "the dreamer" (#43-#47; brought to main by #66 as a GUI-only
// term). NOT part of the engine: basic_eval() does not call it and nothing in
// the search includes this file. The GUI shows it beside the static eval when
// its "Dreamer" switch is on (gui/session.hpp). It reads the eval's internals
// (db is a change of piece tables + activity + pawn structure, and
// src/plan_eval.cpp keeps an incremental copy of piece_activity_eval()'s role
// terms), so a change to src/basic_eval.cpp must be followed here; the GUI's
// self-check (gui/eval_split.hpp) compares the fast db with the reference one.
//
// Plan eval (#43): a piece is also worth something for the better square it
// could reach in a few moves, not only for where it stands.
//
// For every piece of both sides: over every square t it can reach, db is the
// gain of moving just that piece from its square to t (all other pieces fixed)
// in piece tables + piece activity + pawn structure - no material, no king
// safety; with the net (with_nne below) also its correction - from its
// owner's point of view, and N is the least number of moves
// it needs. The piece adds the best db/(2+N), or nothing when db <= 0
// everywhere, for both sides alike (#46: dividing the side to move's pieces by
// 1+N swung the term ~95 cp every ply). The side to move gets PLAN_TEMPO.
//
// Paths are pseudo-legal moves with all other pieces fixed (own-king safety is
// ignored), through empty squares only, so no captures. A piece other than a
// pawn never passes through or stops on a square attacked by an enemy pawn.
// Pawns only push (single, or double from the start rank) and stop before the
// last rank, since a promotion is not a square gain. No castling. A king has
// no target while the enemy has a queen (#47 Q3: db leaves king safety out).
// A square is the target of one piece per side (#47 Q2): greedily by term,
// each piece takes its best square that no higher-termed piece took.
// The sum, tempo included, is scaled by PLAN_SCALE/100 = 3/4 (#47 Q5).
//
// db is exactly the change of those three partial evals, but computed from the
// terms the move touches rather than by re-evaluating the board per target
// (see src/plan_eval.cpp); `reference` in plan_eval_detail() re-evaluates
// instead, which is the definition and ~10x slower. With PLAN_OWN_ACTIVITY the
// activity part is only the moved piece's own (below), in both paths.

#ifndef PLAN_TEMPO_CP
#define PLAN_TEMPO_CP 10
#endif
constexpr int PLAN_TEMPO = PLAN_TEMPO_CP;// cp for the side to move, part of the plan term

// |plan term| <= PLAN_BOUND: the term is clamped to it (on the line, so that
// a lazy eval could decide a node from the rest of the eval). Never reached
// on the bench (max 140 cp at depth 7).
constexpr int PLAN_BOUND = 150;

// Variants of the term (#47), each a -D switch so a match needs no commit.
#ifndef PLAN_MAX_N
#define PLAN_MAX_N 0
#endif
#ifndef PLAN_ONE_PER_SQUARE
#define PLAN_ONE_PER_SQUARE 1
#endif
#ifndef PLAN_KING_HOME
#define PLAN_KING_HOME 1
#endif
#ifndef PLAN_AVOID_LOWER
#define PLAN_AVOID_LOWER 0
#endif
#ifndef PLAN_SCALE_PCT
#define PLAN_SCALE_PCT 75
#endif
#ifndef PLAN_Q2_CONFLICT_ONLY
#define PLAN_Q2_CONFLICT_ONLY 0
#endif
#ifndef PLAN_OWN_ACTIVITY
#define PLAN_OWN_ACTIVITY 1
#endif
// A1: targets at most this many moves away; 0 = no cap.
constexpr int PLAN_N_CAP = PLAN_MAX_N;
// Q2: a square is some piece's target once per side. Each piece keeps its
// PLAN_CANDIDATES best targets; greedily by term, a piece takes its best one
// that no piece with a higher term took.
constexpr bool PLAN_Q2_ONE_PER_SQUARE = PLAN_ONE_PER_SQUARE;
constexpr int PLAN_CANDIDATES = 4;
// PLAN_Q2_CONFLICT_ONLY: Q2 without the candidate list. Each piece keeps its
// best target and the runner-up; when a higher-termed piece of its side took
// the best, it takes the runner-up if that is free, and otherwise searches
// again with the taken squares excluded. The same greedy as the list, except
// that it never runs out of candidates (the list gives up after 4, 5% of
// positions). Clashes are the rule (96% of evals, ~1.5 searches repeated per
// eval), and the term is only ~5% cheaper.
constexpr int PLAN_Q2_NONE = 0, PLAN_Q2_LIST = 1, PLAN_Q2_CONFLICTS = 2;
constexpr int PLAN_Q2_MODE = !PLAN_Q2_ONE_PER_SQUARE ? PLAN_Q2_NONE : PLAN_Q2_CONFLICT_ONLY ? PLAN_Q2_CONFLICTS : PLAN_Q2_LIST;
// PLAN_OWN_ACTIVITY: db's activity part is only the moved piece's own role
// terms, at its target minus at its square; every other piece's activity
// stays as it is in the position (no slider that sees further when the piece
// leaves, no role that gains or loses the piece from its mask). The piece
// tables and the pawn-support part are unchanged. ~35% cheaper.
constexpr bool PLAN_OWN_ACT = PLAN_OWN_ACTIVITY;
// Q3: a king has no target while the enemy has a queen.
constexpr bool PLAN_Q3_KING_HOME = PLAN_KING_HOME;
// Q4: a piece also never passes through or stops on a square attacked by an
// enemy piece of lower value (the king: by any enemy piece).
constexpr bool PLAN_Q4_AVOID_LOWER = PLAN_AVOID_LOWER;
// Q5: the whole term, tempo included, times PLAN_SCALE/100.
constexpr int PLAN_SCALE = PLAN_SCALE_PCT;

// One piece's best target, for the probe.
struct Plan_Target
{
    int piece;   // Board[] index, 0..11
    int from;
    int to;      // -1 when the piece adds nothing
    int n;       // moves needed to reach `to`
    int db;      // gain at `to` (owner's view, > 0 when to != -1)
    int term;    // db/(2+N); >= 0
};

// The plan term in white's view (centipawns).
int plan_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);

// Same sum, and each piece's best target written to out[0..*count-1] when out
// is given (it must hold 32 entries).
// q2_mode picks the Q2 selection (PLAN_Q2_*), for the probe to compare them.
// with_nne (#67): db also counts the change of the net's correction
// (nne::correction(), so a net must be loaded) on the board with the piece at
// its target, so the dream squares are picked by static eval + net, the eval
// the search's quiet leaves use with UseNNE. One net evaluation per target.
int plan_eval_detail(const BB* const original, const WEIGHTS& W, Plan_Target* out, int* count, bool reference = false, int q2_mode = PLAN_Q2_MODE, bool with_nne = false);

// One shortest path of the piece in `t` to its target, for the GUI's plan view:
// squares[0] = t.from ... squares[t.n] = t.to, returns t.n+1 (0 when t.to == -1).
// *via gets every square on some shortest path, both ends included. Same rules
// as plan_eval(): other pieces fixed, empty squares, no enemy-pawn squares.
int plan_path(const BB* const original, const Plan_Target& t, int* squares, uint64_t* via);

// piece tables + piece activity + pawn structure, white's view: what db is a change of.
int plan_partial_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);

#include "../src/plan_eval.cpp"
#endif // PLAN_EVAL_HPP
