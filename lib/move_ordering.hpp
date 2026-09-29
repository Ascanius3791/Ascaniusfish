// OWNERSHIP=Claude
#ifndef MOVE_ORDERING_HPP
#define MOVE_ORDERING_HPP
// Staged move ordering for minimax(): the moves after the TT move come in the
// order
//   1. good captures (SEE > 0) and queen promotions, best first (SEE, then MVV-LVA,
//      all in the fixed SEE_PIECE_VALUE material units)
//   2. even captures (SEE == 0)
//   3. killer moves (quiet moves that caused a beta cutoff at this ply)
//   4. the other quiet moves (and under-promotions), by history score
//   5. bad captures (SEE < 0), least bad first
// No stage evaluates a position: stages 1-3 cost a bit test per move and a
// SEE per capture, stage 4 a table lookup per move. Stage 4 is sorted only
// once the search reaches it, i.e. when nothing before it cut off.
// (sorting_eval() for stage 4 at remaining depth >= 2..5 saved no nodes at
// bench depth 7, see #10.)
#include "Bitboards.hpp"
#include "Weights.hpp"
#include "Settings.hpp"
#include "move_generation.hpp"
#include "see.hpp"
#include <vector>

constexpr int MAX_ORDERED_MOVES = 256;  // more than the 218 legal moves any position can have

// Two killers per ply, ply counted from the root of the current search.
inline Move killer_moves[MAX_SEARCH_PLY][2];

// Butterfly history, [white to move][from][to]: how much a quiet move cut
// off lately, weighted by remaining depth. Kept within +-HISTORY_MAX.
constexpr int HISTORY_MAX = 16384;
inline int quiet_history[2][64][64];

void clear_killer_moves();  // killers and history, at the start of every search ("go")
void store_killer_move(int ply_from_root, const Move& move);
bool is_quiet_move(const BB* const parent, const Move& move);  // no capture, no promotion

// minimax_tactical()'s ordering of its captures and queen promotions (the n
// `indices` into moves): pure material - SEE (incl. promotion gain), then MVV-LVA.
void order_tactical_moves(const BB* const parent, const Move* const moves, int* const indices, int n);
// Old vector form, until minimax_tactical() moves to Move_List (#34).
inline void order_tactical_moves(const BB* const parent, const std::vector<Move>& moves, std::vector<int>& indices)
{ order_tactical_moves(parent, moves.data(), indices.data(), (int)indices.size()); }

class Staged_Move_Order;
// A quiet move cut off at `depth`: it becomes a killer and gains history, the
// quiet moves searched before it at this node lose history.
void store_quiet_cutoff(const BB* const parent, int ply_from_root, int depth, const Move& move,
                        const Staged_Move_Order& order);

class Staged_Move_Order
{
    public:
    // Orders moves[0..num), which must stay in place until the search is done
    // with them; `skip_index` is the already searched TT move, or -1.
    void init(const BB* const parent, const Move* const moves, int num, int skip_index, int ply_from_root);
    // Old vector form, until minimax() moves to Move_List (#34).
    void init(const BB* const parent, const BB* const, const std::vector<Move>& moves, int num,
              int skip_index, int ply_from_root, const WEIGHTS&)
    { init(parent, moves.data(), num, skip_index, ply_from_root); }
    int next();  // index into moves of the next move to search, -1 when done

    private:
    friend void store_quiet_cutoff(const BB* const, int, int, const Move&, const Staged_Move_Order&);
    void sort_quiets();

    const Move* moves = nullptr;
    bool white_move = true;
    int order[MAX_ORDERED_MOVES];
    int count = 0, cursor = 0;
    int killer_begin = 0;                // stage 3 is order[killer_begin..quiet_begin)
    int quiet_begin = 0, quiet_end = 0;  // stage 4 is order[quiet_begin..quiet_end)
    int quiet_tt_index = -1;             // the TT move, if it is quiet
    bool quiets_sorted = false;
};

#include "../src/move_ordering.cpp"

#endif // MOVE_ORDERING_HPP
