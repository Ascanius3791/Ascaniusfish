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
    uint32_t move = 0;         // packed, see pack()

    TT_Result() = default;
    TT_Result(const PV_Line& line) { *this = line; }
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
#endif

// An exact mate or table score (#78, #39): it holds at every depth, which is why
// minimax() cuts on it at any depth. Works for both TT_Result layouts.
inline bool tt_proven(const TT_Result& r)
{
    return r.bound_type==0 && (r.eval<=INT_MIN+max_mating_seq || r.eval>=INT_MAX-max_mating_seq);
}

#endif // TT_RESULT_HPP
