// OWNERSHIP=Claude
#ifndef PLAN_EVAL_HPP
#define PLAN_EVAL_HPP
#include "basic_eval.hpp"

// Plan eval (#43): a piece is also worth something for the better square it
// could reach in a few moves, not only for where it stands.
//
// For every piece of both sides: over every square t it can reach, db is the
// gain of moving just that piece from its square to t (all other pieces fixed)
// in piece tables + piece activity + pawn structure - no material, no king
// safety - from its owner's point of view, and N is the least number of moves
// it needs. The piece adds the best db/(2+N), or nothing when db <= 0
// everywhere. The side to move is one tempo closer: its pieces divide by 1+N.
//
// Paths are pseudo-legal moves with all other pieces fixed (own-king safety is
// ignored), through empty squares only, so no captures. A piece other than a
// pawn never passes through or stops on a square attacked by an enemy pawn.
// Pawns only push (single, or double from the start rank) and stop before the
// last rank, since a promotion is not a square gain. No castling.
//
// db is exactly the change of those three partial evals, but computed from the
// terms the move touches rather than by re-evaluating the board per target
// (see src/plan_eval.cpp); `reference` in plan_eval_detail() re-evaluates
// instead, which is the definition and ~10x slower.

constexpr bool USE_PLAN_EVAL = true;

// One piece's best target, for the probe.
struct Plan_Target
{
    int piece;   // Board[] index, 0..11
    int from;
    int to;      // -1 when the piece adds nothing
    int n;       // moves needed to reach `to`
    int db;      // gain at `to` (owner's view, > 0 when to != -1)
    int term;    // db/(2+N), or db/(1+N) for the side to move; >= 0
};

// The plan term in white's view (centipawns). 0 when USE_PLAN_EVAL is false.
int plan_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);

// Same sum, and each piece's best target written to out[0..*count-1] when out
// is given (it must hold 32 entries). Computed whatever USE_PLAN_EVAL says.
int plan_eval_detail(const BB* const original, const WEIGHTS& W, Plan_Target* out, int* count, bool reference = false);

// One shortest path of the piece in `t` to its target, for the GUI's plan view:
// squares[0] = t.from ... squares[t.n] = t.to, returns t.n+1 (0 when t.to == -1).
// *via gets every square on some shortest path, both ends included. Same rules
// as plan_eval(): other pieces fixed, empty squares, no enemy-pawn squares.
int plan_path(const BB* const original, const Plan_Target& t, int* squares, uint64_t* via);

// piece tables + piece activity + pawn structure, white's view: what db is a change of.
int plan_partial_eval(const BB* const original, const WEIGHTS& W = WEIGHTS_OG);

#include "../src/plan_eval.cpp"
#endif // PLAN_EVAL_HPP
