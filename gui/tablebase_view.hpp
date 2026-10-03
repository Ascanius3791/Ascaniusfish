// OWNERSHIP=Claude
// What the Syzygy tables say about the position on the board (issue #40).
//
// The server links lib/syzygy.hpp itself, so this needs no search: a position
// with few enough pieces has its exact result the moment the cursor lands on
// it, with the analysis switched off. The tables are opened once, at start-up
// (`syzygy=<dir>`), and are shared by every session; whether a session *uses*
// them, and up to how many pieces, is its own setting (Session::tb_on/tb_limit).
//
// Everything here is for the side to move except where a name says white. The
// page draws white's view, so Session converts.
#ifndef GUI_TABLEBASE_VIEW_HPP
#define GUI_TABLEBASE_VIEW_HPP
#include "../tools/game_rules.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

// The directory given as syzygy=, how many tables came out of it, and — when
// the switch has to be off — the sentence the gear says why.
struct Tablebase_Setup
{
    std::string dir;
    int tables = 0;
    std::string reason = "No tablebase directory given (start the server with syzygy=<dir>, or make gui SYZYGY=<dir>).";

    bool available() const { return tables>0; }

    void load(const std::string& directory)
    {
        dir = directory;
        tables = syzygy::init(directory);
        if(tables==0)
        reason = "No Syzygy tables found in " + directory + ".";
        else
        reason.clear();
    }

    // The most pieces the loaded tables can answer for, whatever the gear asks.
    int max_pieces() const { return syzygy::max_pieces(); }

    // The Gaviota DTM tables (#77, `gaviota=<dir>`): the distance to mate next
    // to Syzygy's result. They only add to it, so they count only while the
    // Syzygy tables are on too.
    std::string dtm_dir;
    int dtm_pieces = 0;

    void load_dtm(const std::string& directory)
    {
        dtm_dir = directory;
        dtm_pieces = gaviota::init(directory, 32);
    }
};

inline Tablebase_Setup& tablebase_setup()
{
    static Tablebase_Setup setup;
    return setup;
}

constexpr int TB_LIMIT_MIN = 3, TB_LIMIT_MAX = 5;

inline int piece_count(const BB& pos)
{
    int n = 0;
    for(int i=0;i<12;i++)
    n += __builtin_popcountll(pos.Board[i]);
    return n;
}

// One position's result for the side to move.
struct Tb_Result
{
    bool valid = false;
    int wdl = 0;   // -2 loss .. 2 win, see syzygy::WDL
    int dtz = 0;   // plies to the next capture or pawn move, signed like the win/loss; 0 = draw
};

inline Tb_Result tb_probe(const BB& pos, int limit)
{
    Tb_Result r;
    if(piece_count(pos)>std::min(limit, syzygy::max_pieces()))
    return r;
    int wdl = 0, dtz = 0;
    if(!syzygy::probe_wdl(&pos, wdl) || !syzygy::probe_dtz(&pos, dtz))
    return r;
    r.valid = true;
    r.wdl = wdl;
    r.dtz = dtz;
    return r;
}

// One legal move and what it leads to, from the mover's point of view.
struct Tb_Move
{
    int index = 0;   // into the position's legal moves
    int wdl = 0;
    int dtz = 0;     // plies until the next zeroing move counting this one, as a distance (0 = draw)
};

// Every legal move of `pos` with its result, best first. `children` are the
// positions after each move (the engine's own move generator's). False when
// the position itself is unknown or any child is — a list with holes in it
// would rank moves against ones it left out.
inline bool tb_moves(const BB& pos, const BB* children, int n, int limit, std::vector<Tb_Move>& out)
{
    out.clear();
    if(!tb_probe(pos, limit).valid)
    return false;
    for(int k=0;k<n;k++)
    {
        Tb_Move m;
        m.index = k;
        const BB& child = children[k];
        bool zeroing = piece_count(child)!=piece_count(pos)
                    || child.Board[0]!=pos.Board[0] || child.Board[6]!=pos.Board[6];
        int wdl = 0, dtz = 0;
        // A child with castling rights left cannot be probed; neither can the
        // parent, so this only happens for a table that is missing.
        if(piece_count(child)>syzygy::max_pieces() || !syzygy::probe_wdl(&child, wdl) || !syzygy::probe_dtz(&child, dtz))
        {
            out.clear();
            return false;
        }
        m.wdl = -wdl;
        int away = std::abs(dtz);
        // Mating the opponent ends the game; a capture or pawn move is the
        // zeroing move itself. Anything else is one ply plus what is left.
        m.dtz = wdl==0 ? 0 : (dtz==-1 || zeroing) ? 1 : away+1;
        out.push_back(m);
    }
    // Wins by the shortest DTZ first, losses by the longest, draws in between.
    std::stable_sort(out.begin(), out.end(), [](const Tb_Move& a, const Tb_Move& b)
    {
        if(a.wdl!=b.wdl)
        return a.wdl>b.wdl;
        return a.wdl>0 ? a.dtz<b.dtz : a.dtz>b.dtz;
    });
    return true;
}

// What the Gaviota tables say about one position (#77): the mate distance of it
// and of every legal move, and the mating line itself (the quickest mate for
// the winner, the longest resistance for the loser, ignoring the 50-move rule).
// Plies, signed for the side that has them: > 0 mates, < 0 is mated.
struct Dtm_View
{
    bool valid = false;
    int mate = 0;                         // the side to move's; 0 = a draw
    std::vector<int> moves;               // per legal move index, the mover's after it; 0 = a draw
    std::vector<std::string> uci, san;    // the line from the position
};

constexpr int DTM_LINE_MAX = 64;  // plies of the line; the longest 5-piece mates are longer

// The positions after pos's legal moves, and their DTM (the opponent's).
inline bool dtm_children(const BB& pos, BB* children, int& n, std::vector<int>& res, std::vector<int>& plies)
{
    n = std::get<0>(all_moves(&pos, children, MAX_ORDERED_MOVES));
    res.assign(n, 0);
    plies.assign(n, 0);
    for(int i=0;i<n;i++)
    if(!gaviota::probe_dtm(&children[i], res[i], plies[i]))
    return false;
    return true;
}

// The child the side to move plays on the mating line: the quickest one the
// opponent loses, else the slowest one they win; -1 if every move draws.
inline int dtm_pick(const std::vector<int>& res, const std::vector<int>& plies)
{
    int win = -1, lose = -1;
    for(int i=0;i<(int)res.size();i++)
    {
        if(res[i]==-1 && (win<0 || plies[i]<plies[win])) win = i;
        if(res[i]==1 && (lose<0 || plies[i]>plies[lose])) lose = i;
    }
    return win>=0 ? win : lose;
}

inline Dtm_View dtm_view(const BB& pos, int limit)
{
    Dtm_View v;
    if(piece_count(pos)>std::min(limit, tablebase_setup().dtm_pieces))
    return v;
    int res, plies;
    if(!gaviota::probe_dtm(&pos, res, plies))
    return v;
    BB children[MAX_ORDERED_MOVES];
    int n;
    std::vector<int> cres, cplies;
    if(!dtm_children(pos, children, n, cres, cplies))
    return v;
    v.valid = true;
    v.mate = res*plies;
    for(int i=0;i<n;i++)
    v.moves.push_back(cres[i]==0 ? 0 : -cres[i]*(cplies[i]+1));
    if(res==0)
    return v;
    BB at = pos;
    for(int k = dtm_pick(cres, cplies); k>=0 && (int)v.uci.size()<DTM_LINE_MAX; )
    {
        v.uci.push_back(get_UCI(&at, &children[k]));
        v.san.push_back(san(at, children, n, k));
        at = children[k];
        if(!dtm_children(at, children, n, cres, cplies))
        break;
        k = dtm_pick(cres, cplies);
    }
    return v;
}

// Plies to mate as the moves a "#N" counts: the winner's last move is the
// mating one, so mate in 1 ply is #1, and being mated in 2 plies is #-1.
inline int mate_moves(int plies)
{
    return plies>0 ? (plies+1)/2 : -((-plies)/2);
}

inline const char* tb_wdl_name(int wdl)   // for the side that has it
{
    return wdl==2 ? "win" : wdl==1 ? "cursed win" : wdl==0 ? "draw"
         : wdl==-1 ? "blessed loss" : "loss";
}

#endif // GUI_TABLEBASE_VIEW_HPP
