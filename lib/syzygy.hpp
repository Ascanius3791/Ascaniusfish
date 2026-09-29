// OWNERSHIP=Claude
#ifndef SYZYGY_HPP
#define SYZYGY_HPP
#include "move_generation.hpp"
#include <string>

// Syzygy endgame tablebase prober (issue #37), written from the file format:
// no Fathom, no Stockfish code. Reads the .rtbw (WDL) and .rtbz (DTZ) files of
// the 3-4-5 piece set, e.g. from tablebase.lichess.ovh/tables/standard/.
//
// The tables are mmap'd read-only, so opening all of them costs only the pages
// the headers sit on; the rest is paged in by the probes that need it.
//
// What the files hold, and what the probe has to add itself:
//   - A WDL file holds one value per position and side to move, but where a
//     capture is the best move the stored value is "don't care" (whatever
//     compressed best). So every probe first searches the captures (recursively,
//     into the smaller tables), and uses the table only when no capture is at
//     least as good. En passant is not in the tables at all; it is searched as
//     a capture like the others.
//   - A DTZ file holds one side to move only. The other side gets a 1-ply
//     search over the stored side's positions.
//   - Positions are stored up to symmetry: colours swapped when the stronger
//     side is black (KRvKQ is read from KQvKR), mirrored files, and for tables
//     without pawns also mirrored ranks and the a1-h8 diagonal.

namespace syzygy
{

// Game-theoretic value for the side to move. The two middle values are the
// 50-move rule's: a cursed win is a win that takes more than 100 plies without
// a capture or pawn move (a draw under the rule), a blessed loss its mirror.
enum WDL : int
{
    WDL_LOSS = -2,
    WDL_BLESSED_LOSS = -1,
    WDL_DRAW = 0,
    WDL_CURSED_WIN = 1,
    WDL_WIN = 2
};

// Tables with more pieces than this are not loaded.
constexpr int MAX_PIECES = 5;

// Maps every *.rtbw/*.rtbz file with at most MAX_PIECES pieces in `dir` and
// parses their headers. Returns the number of WDL tables loaded; a file that
// fails to parse is reported on stderr and skipped. Calling it again first
// releases the tables of the previous call.
int init(const std::string& dir);

// Unmaps every table.
void release();

// The most pieces any loaded WDL table has (0 = none loaded).
int max_pieces();

// Tables loaded by init(): WDL files and DTZ files.
int wdl_table_count();
int dtz_table_count();

// WDL of pos for the side to move, independent of the halfmove clock (as if it
// were 0). False ("not found") if pos has castling rights, more pieces than
// max_pieces(), or a material whose table is missing. KvK is a draw.
bool probe_wdl(const BB* const pos, int& wdl);

// DTZ of pos in plies for the side to move, with the sign of its WDL: the
// distance to the next capture or pawn move (a zeroing move) on the fastest
// winning / slowest losing line, assuming the halfmove clock is 0.
//      -1          the side to move is mated
//   -100 .. -2     loss                 2 .. 100    win
//   below -100     blessed loss         above 100   cursed win (plus 100)
//       0          draw
// The standard Syzygy DTZ files store some distances in moves, not plies; then
// the true distance may be one ply longer than the value returned (never across
// the 100-ply boundary, which the WDL decides), and *rounded is set. The
// "no rounding" DTZ files (tablebase.lichess.ovh 3-4-5-dtz-nr) store plies
// throughout and give the exact value. False exactly when probe_wdl() is false,
// or the .rtbz file is missing.
bool probe_dtz(const BB* const pos, int& dtz, bool* rounded = nullptr);

} // namespace syzygy

#include "../src/syzygy.cpp"

#endif // SYZYGY_HPP
