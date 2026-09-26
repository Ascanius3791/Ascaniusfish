// OWNERSHIP=Claude
#ifndef MOVE_ORDERING_HPP
#define MOVE_ORDERING_HPP
// Staged move ordering for minimax(): the moves after the TT move come in the
// order
//   1. good captures (SEE > 0) and queen promotions, best first
//   2. even captures (SEE == 0)
//   3. killer moves (quiet moves that caused a beta cutoff at this ply)
//   4. the other quiet moves (and under-promotions), sorted by sorting_eval()
//   5. bad captures (SEE < 0), least bad first
// Stages 1-3 only cost a bit test per move and a SEE per capture. The
// expensive sorting_eval() of stage 4 runs only once the search actually
// reaches stage 4, i.e. when nothing before it cut off.
#include "Bitboards.hpp"
#include "Weights.hpp"
#include "Settings.hpp"
#include "move_generation.hpp"
#include "see.hpp"
#include "basic_eval.hpp"
#include <vector>

constexpr int MAX_ORDERED_MOVES = 256;  // more than the 218 legal moves any position can have

// Two killers per ply, ply counted from the root of the current search.
inline Move killer_moves[MAX_SEARCH_PLY][2];

void clear_killer_moves();  // at the start of every search ("go")
void store_killer_move(int ply_from_root, const Move& move);
bool is_quiet_move(const BB* const parent, const Move& move);  // no capture, no promotion

class Staged_Move_Order
{
    public:
    // `children` are the positions after moves[0..num) (as filled in by
    // all_moves()); `skip_index` is the already searched TT move, or -1.
    void init(const BB* const parent, const BB* const children, const std::vector<Move>& moves, int num,
              int skip_index, int ply_from_root, const WEIGHTS& W);
    int next();  // index into moves of the next move to search, -1 when done

    private:
    void sort_quiets();

    const BB* children = nullptr;
    const WEIGHTS* W = nullptr;
    bool white_move = true;
    int order[MAX_ORDERED_MOVES];
    int count = 0, cursor = 0;
    int quiet_begin = 0, quiet_end = 0;  // stage 4 is order[quiet_begin..quiet_end)
    bool quiets_sorted = false;
};

#include "../src/move_ordering.cpp"

#endif // MOVE_ORDERING_HPP
