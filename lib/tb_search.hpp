// OWNERSHIP=Claude
#ifndef TB_SEARCH_HPP
#define TB_SEARCH_HPP

#include "syzygy.hpp"
#include "Settings.hpp"
#include <climits>

// Tablebase results inside the search (issue #39). minimax() asks
// tb_probe_score() at every node below the root; when it answers, the node is
// over and the score is that of the table.
//
// Scores, in white's view like every other eval: a TB win is TB_WIN_SCORE, a
// TB loss TB_LOSS_SCORE, the edges of the mate band: the longest mate there can
// be. So every test for a proven mate (eval >= INT_MAX - max_mating_seq, or <=
// INT_MIN + max_mating_seq) counts a TB result as one, while the search's
// one-per-ply shift (strictly inside the band) leaves it alone: any mate the
// search sees beats it. The score does not depend on the ply it was found at,
// so a TT entry holding one is right at any other ply too.
// TB_LOSS_SCORE is not -TB_WIN_SCORE: that is INT_MIN + max_mating_seq + 1,
// one outside the band.
constexpr int TB_WIN_SCORE = INT_MAX - max_mating_seq;
constexpr int TB_LOSS_SCORE = INT_MIN + max_mating_seq;

// SyzygyProbeLimit: the most pieces (both kings included) a node may have to be
// probed. 0 = no probing in the search. The tables hold at most 5, so more
// makes no difference. Set by "setoption" while no search runs.
inline int tb_probe_limit = 5;

// Probes answered with a result since the process started (UCI "tbhits").
inline long long tb_hits = 0;

// UCI's number for a TB score: a large cp, never "mate" (it says nothing of
// how many moves the mate takes).
constexpr int TB_WIN_CP = 10000;

inline bool is_tb_score(int eval)
{
    return eval == TB_WIN_SCORE || eval == TB_LOSS_SCORE;
}

// The tables' result of `pos` in white's view, when they know it: at most
// tb_probe_limit pieces and no castling rights (the tables have none). Probed
// whatever the halfmove clock: the result is the one with the clock at 0, so
// with a clock running a win may in fact already be a draw under the 50-move
// rule. That is accepted, so that every node with few enough pieces gets the
// table's score rather than the eval's. A cursed win or blessed loss is a
// draw, which is what it is under that rule even from a clock of 0.
inline bool tb_probe_score(const BB* const pos, int& white_score)
{
    const int limit = std::min(tb_probe_limit, syzygy::max_pieces());  // 0 with no tables loaded
    if(limit<=0)
    return false;
    int pieces = 0;
    for(int i=0;i<12;i++)
    pieces += __builtin_popcountll(pos->Board[i]);
    if(pieces>limit)
    return false;
    int wdl;
    if(!syzygy::probe_wdl(pos, wdl))
    return false;
    tb_hits++;
    const bool white_wins = (wdl==syzygy::WDL_WIN) == pos->white_move;
    white_score = wdl!=syzygy::WDL_WIN && wdl!=syzygy::WDL_LOSS ? 0 : white_wins ? TB_WIN_SCORE : TB_LOSS_SCORE;
    return true;
}

#endif // TB_SEARCH_HPP
