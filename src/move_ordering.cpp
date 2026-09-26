// OWNERSHIP=Claude
#ifndef MOVE_ORDERING_CPP
#define MOVE_ORDERING_CPP
#include "../lib/move_ordering.hpp"
#include <algorithm>
#include <cstdlib>

constexpr int QUEEN_PROMOTION = 4;  // promotion_piece_type of a queen promotion (= queen's board index)

void clear_killer_moves()
{
    for(int p=0;p<MAX_SEARCH_PLY;p++)
    killer_moves[p][0] = killer_moves[p][1] = Move();
    for(int side=0;side<2;side++)
    for(int from=0;from<64;from++)
    for(int to=0;to<64;to++)
    quiet_history[side][from][to] = 0;
}

void store_killer_move(int ply_from_root, const Move& move)
{
    if(ply_from_root<0 || ply_from_root>=MAX_SEARCH_PLY || move==killer_moves[ply_from_root][0])
    return;
    killer_moves[ply_from_root][1] = killer_moves[ply_from_root][0];
    killer_moves[ply_from_root][0] = move;
}

bool is_quiet_move(const BB* const parent, const Move& move)
{
    return move.promotion_piece_type<=0
        && !is_capturing_move(parent->Board, move.to, parent->white_move, move.is_en_passant);
}

// "History gravity": the bigger the entry already is, the less a bonus of the
// same sign adds, so entries stay within +-HISTORY_MAX and old results fade.
static void update_history(int& entry, int bonus)
{
    entry += bonus - entry*std::abs(bonus)/HISTORY_MAX;
}

void store_quiet_cutoff(const BB* const parent, int ply_from_root, int depth, const Move& move,
                        const Staged_Move_Order& order)
{
    store_killer_move(ply_from_root, move);
    int (&history)[64][64] = quiet_history[parent->white_move];
    const int bonus = std::min(std::max(depth,1)*std::max(depth,1), HISTORY_MAX/4);
    update_history(history[move.from][move.to], bonus);

    // Quiet moves searched before `move`. If the TT move cut off, the order was
    // never initialised and there are none.
    if(order.moves==nullptr)
    return;
    const std::vector<Move>& moves = *order.moves;
    if(order.quiet_tt_index>=0)
    update_history(history[moves[order.quiet_tt_index].from][moves[order.quiet_tt_index].to], -bonus);
    const int searched_end = std::min(order.cursor, order.quiet_end);
    for(int k=order.killer_begin;k<searched_end;k++)
    {
        const Move& m = moves[order.order[k]];
        if(m.promotion_piece_type<=0 && !(m==move))
        update_history(history[m.from][m.to], -bonus);
    }
}

// Board index (0-5, colour stripped) of the piece on `square` among Board[offset..offset+6).
static int piece_on(const uint64_t Board[12], int offset, int square)
{
    for(int p=0;p<6;p++)
    if(Board[offset+p] & (1ULL<<square))
    return p;
    return 0;
}

struct Scored_Capture
{
    int index;
    int see;       // stage key; queen promotions get the promotion gain added
    int victim;    // MVV-LVA tie-break
    int attacker;
};

// Best first: higher SEE, then more valuable victim, then less valuable attacker.
static void sort_captures(Scored_Capture* c, int n)
{
    std::sort(c, c+n, [](const Scored_Capture& a, const Scored_Capture& b)
    {
        if(a.see!=b.see) return a.see>b.see;
        if(a.victim!=b.victim) return a.victim>b.victim;
        return a.attacker<b.attacker;
    });
}

void Staged_Move_Order::init(const BB* const parent, const BB* const children, const std::vector<Move>& moves, int num,
                             int skip_index, int ply_from_root, const WEIGHTS& W)
{
    this->moves = &moves;
    this->children = children;
    this->W = &W;
    white_move = parent->white_move;
    count = cursor = 0;
    quiets_sorted = false;
    quiet_tt_index = skip_index>=0 && is_quiet_move(parent, moves[skip_index]) ? skip_index : -1;

    Scored_Capture good[MAX_ORDERED_MOVES], even[MAX_ORDERED_MOVES], bad[MAX_ORDERED_MOVES];
    int quiet[MAX_ORDERED_MOVES];
    int n_good = 0, n_even = 0, n_bad = 0, n_quiet = 0;
    int killer_index[2] = {-1, -1};
    bool has_killers = ply_from_root>=0 && ply_from_root<MAX_SEARCH_PLY;
    const int own = white_move ? 0 : 6, enemy = white_move ? 6 : 0;

    for(int i=0;i<num;i++)
    {
        if(i==skip_index)
        continue;
        const Move& m = moves[i];
        bool capture = is_capturing_move(parent->Board, m.to, white_move, m.is_en_passant);
        bool queen_promotion = m.promotion_piece_type==QUEEN_PROMOTION;
        if(capture || queen_promotion)
        {
            Scored_Capture c;
            c.index = i;
            c.see = capture ? static_exchange_eval(parent->Board, m.from, m.to, white_move, m.is_en_passant, W) : 0;
            int victim_square = m.is_en_passant ? (white_move ? m.to-8 : m.to+8) : m.to;
            c.victim = capture ? W.piece_value[piece_on(parent->Board, enemy, victim_square)] : 0;
            c.attacker = W.piece_value[piece_on(parent->Board, own, m.from)];
            if(queen_promotion)
            {
                c.see += W.piece_value[QUEEN_PROMOTION]-W.piece_value[0];
                good[n_good++] = c;
            }
            else if(c.see>0) good[n_good++] = c;
            else if(c.see==0) even[n_even++] = c;
            else bad[n_bad++] = c;
        }
        else if(has_killers && m.promotion_piece_type<=0 && m==killer_moves[ply_from_root][0])
        killer_index[0] = i;
        else if(has_killers && m.promotion_piece_type<=0 && m==killer_moves[ply_from_root][1])
        killer_index[1] = i;
        else
        quiet[n_quiet++] = i;
    }

    sort_captures(good, n_good);
    sort_captures(even, n_even);
    sort_captures(bad, n_bad);
    for(int k=0;k<n_good;k++) order[count++] = good[k].index;
    for(int k=0;k<n_even;k++) order[count++] = even[k].index;
    killer_begin = count;
    for(int k=0;k<2;k++) if(killer_index[k]>=0) order[count++] = killer_index[k];
    quiet_begin = count;
    for(int k=0;k<n_quiet;k++) order[count++] = quiet[k];
    quiet_end = count;
    for(int k=0;k<n_bad;k++) order[count++] = bad[k].index;
}

void Staged_Move_Order::sort_quiets()
{
    quiets_sorted = true;
    int score[MAX_ORDERED_MOVES];
    for(int k=quiet_begin;k<quiet_end;k++)
    score[order[k]] = sorting_eval(children+order[k], *W);
    const bool wm = white_move;
    std::sort(order+quiet_begin, order+quiet_end, [&score, wm](int a, int b)
    {
        return wm ? score[a]>score[b] : score[a]<score[b];
    });
}

int Staged_Move_Order::next()
{
    if(cursor>=count)
    return -1;
    if(cursor==quiet_begin && !quiets_sorted)
    sort_quiets();
    return order[cursor++];
}

#endif // MOVE_ORDERING_CPP
