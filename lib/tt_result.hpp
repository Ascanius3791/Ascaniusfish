// OWNERSHIP=Claude
#ifndef TT_RESULT_HPP
#define TT_RESULT_HPP
#include "move_generation.hpp"
#include "Settings.hpp"
#include <climits>
#include <cstdint>

// What a transposition-table entry keeps of a node's result (#82): its best move,
// eval, depth and bound type, but not the rest of the line. The search reads only
// moves[0] of an entry (the TT move in minimax()), so it stays node for node the
// same; only a line that ends at an exact TT hit gets shorter. The PTT
// (gui/ptt.hpp) keeps whole lines.
//
// The members are PV_Line's names, and a TT_Result is assigned from and converts to
// a PV_Line, so every place that writes `entry.pv_line = ...` or reads
// `readout.pv_line` stays as it is. -DTT_FULL_PV=1 stores a whole PV_Line again
// (the entry is ~184 bytes instead of 32), to compare against.
#ifndef TT_FULL_PV
#define TT_FULL_PV 0
#endif

#if TT_FULL_PV
using TT_Result = PV_Line;
#else
struct TT_Result
{
    int eval = 0;
    int depth = 0;
    int8_t bound_type = 2;     // as PV_Line: 0 exact, -1 lower, 1 upper, 2 not set
    int8_t current_lenght = 0; // 1 with a best move, 0 without (null-move cutoff, mate, stalemate, book eval)
    int16_t mark = 0;          // the user's mark (#101), 0 = not set by the user; in what was padding
    uint32_t move = 0;         // packed, see pack()

    TT_Result() = default;
    TT_Result(const PV_Line& line) { *this = line; }
    // A search result, so the mark (which belongs to the position) stays.
    TT_Result& operator=(const PV_Line& line)
    {
        eval = line.eval;
        depth = line.depth;
        bound_type = (int8_t)line.bound_type;
        current_lenght = line.current_lenght>0;
        move = current_lenght ? pack(line.moves[0]) : 0;
        return *this;
    }
    operator PV_Line() const
    {
        PV_Line line = current_lenght ? PV_Line(unpack(move), depth) : PV_Line();
        line.depth = depth;
        line.eval = eval;
        line.bound_type = bound_type;
        return line;
    }
    void clear_extension() {}// nothing to give back: keeps lookup_table_base::reset() as it is

    // from 6 bits, to 6, promotion_piece_type+1 3 (-1..5), castling 1, en passant 1
    static uint32_t pack(const Move& m)
    {
        return (uint32_t)(m.from | m.to<<6 | (m.promotion_piece_type+1)<<12 | m.is_castling<<15 | m.is_en_passant<<16);
    }
    static Move unpack(uint32_t p)
    {
        return Move(p & 63, (p>>6) & 63, (int)((p>>12) & 7) - 1, (p>>15) & 1, (p>>16) & 1);
    }
};
static_assert(sizeof(TT_Result)==16, "the mark lives in what was padding");
#endif

// The user's mark on an entry (#101, the correspondence mode): a marked entry
// outlives deeper unmarked ones (UCI_Table::value_for_victim_index()). A whole
// PV_Line (TT_FULL_PV) has no room for one, so there every entry is unmarked.
#if TT_FULL_PV
inline int16_t tt_mark(const TT_Result&) { return 0; }
inline void set_tt_mark(TT_Result&, int16_t) {}
#else
inline int16_t tt_mark(const TT_Result& r) { return r.mark; }
inline void set_tt_mark(TT_Result& r, int16_t mark) { r.mark = mark; }
#endif

// An exact mate or table score (#78, #39), or a mate claim: a bound saying the
// side that mates does at least that well (a lower bound in white's band, an
// upper bound in black's, #100). Either holds at every depth, which is why
// minimax() cuts on it at any depth. A bound on the other side ("no faster
// mate") is no proof: a pruned search only failed to find one. Works for both
// TT_Result layouts.
inline bool tt_proven(const TT_Result& r)
{
    return (r.bound_type==0 && (r.eval<=INT_MIN+max_mating_seq || r.eval>=INT_MAX-max_mating_seq))
        || (r.bound_type==-1 && r.eval>=INT_MAX-max_mating_seq)
        || (r.bound_type==1 && r.eval<=INT_MIN+max_mating_seq);
}

// Whether `n`, a result for the position of a marked entry holding `o`,
// improves on it (#100). The marked entry is what the user called important,
// so only a better statement replaces it: a shorter mate (the same mate
// exact over a claim of it), a proof over no proof, else a deeper exact
// result, never one over a proof. A deeper bound is not one: its move only
// failed high or low, and the exact result and the saved move would go.
inline bool tt_improves_marked(const TT_Result& n, const TT_Result& o)
{
    const bool n_proof = tt_proven(n), o_proof = tt_proven(o);
    if(n_proof && o_proof)
    {
        if((n.eval>0)!=(o.eval>0))
        return false;
        // plies to the mate; a table score is the edge of the band, the longest
        const long long n_plies = n.eval>0 ? (long long)INT_MAX-n.eval : (long long)n.eval-INT_MIN;
        const long long o_plies = o.eval>0 ? (long long)INT_MAX-o.eval : (long long)o.eval-INT_MIN;
        return n_plies<o_plies || (n_plies==o_plies && n.bound_type==0 && o.bound_type!=0);
    }
    if(n_proof || o_proof)
    return n_proof;
    return n.bound_type==0 && n.depth>o.depth;
}

#endif // TT_RESULT_HPP
