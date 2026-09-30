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
// Score band, in white's view like every other eval: a TB win is
// +TB_WIN_SCORE, a TB loss -TB_WIN_SCORE. It lies below the mate band
// (|eval| > INT_MAX - max_mating_seq, whose scores the search shifts by one per
// ply) and far above any eval, so a real mate beats a TB win and a TB win beats
// everything else. The score does not depend on the ply it was found at, so a
// TT entry holding one is right at any other ply too and needs no adjusting.
constexpr int TB_WIN_SCORE = INT_MAX - 2*max_mating_seq;

// SyzygyProbeLimit: the most pieces (both kings included) a node may have to be
// probed. 0 = no probing in the search. The tables hold at most 5, so more
// makes no difference. Set by "setoption" while no search runs.
inline int tb_probe_limit = 5;

// Probes answered with a result since the process started (UCI "tbhits").
inline long long tb_hits = 0;

// UCI's number for a score in the TB band: a large cp, never "mate".
constexpr int TB_WIN_CP = 10000;

inline bool is_tb_score(int eval)
{
    return (eval >= TB_WIN_SCORE && eval < INT_MAX - max_mating_seq)
        || (eval <= -TB_WIN_SCORE && eval > INT_MIN + max_mating_seq);
}

// The exact result of `pos` in white's view, when the tables know it and it
// would be right to use: at most tb_probe_limit pieces, no castling rights
// (the tables have none) and a halfmove clock of 0 (the tables ignore the
// 50-move rule, so with a clock running a win may already be a draw). A cursed
// win or blessed loss is a draw, which is what it is under that rule.
inline bool tb_probe_score(const BB* const pos, int& white_score)
{
    const int limit = std::min(tb_probe_limit, syzygy::max_pieces());  // 0 with no tables loaded
    if(limit<=0 || pos->halfmoves_since_last_capture_or_pawn_move!=0)
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
    int score = wdl==syzygy::WDL_WIN ? TB_WIN_SCORE : wdl==syzygy::WDL_LOSS ? -TB_WIN_SCORE : 0;
    white_score = pos->white_move ? score : -score;
    return true;
}

#endif // TB_SEARCH_HPP
