// OWNERSHIP=Claude
#ifndef GAVIOTA_HPP
#define GAVIOTA_HPP
#include "move_generation.hpp"
#include <cstddef>
#include <string>

// Gaviota DTM tablebases (issue #77): the distance to mate of a 3-5 piece
// position, which the Syzygy tables (WDL/DTZ) do not hold. The prober is
// third-party C (third_party/gaviota, MIT), built into libgtb.a by the Makefile.
//
// Only a build with -DWITH_GAVIOTA (and -Lthird_party/gaviota -lgtb) has the
// prober; every other build gets the same functions as "no tables loaded", so a
// tool or probe that includes lib/uci.hpp still builds with a plain g++ line.
//
// The tables are read from disk through the prober's own cache (cache_mb), not
// mmap'd, so RSS is that cache plus the tables' indexes (a few MB).

namespace gaviota
{

// Tables with more pieces than this do not exist.
constexpr int MAX_PIECES = 5;

// Opens the *.gtb.cp4 tables in `dir`. Returns the most pieces of a complete
// set found (3, 4 or 5; 0 = none, also when the build has no prober). Calling
// it again first releases the previous tables.
int init(const std::string& dir, size_t cache_mb);

void release();

// As init() returned (0 = none loaded).
int max_pieces();

// Whether this build has the prober at all.
bool compiled_in();

// The side to move's result: 1 win, 0 draw, -1 loss, and on a win or loss the
// plies to mate (0 = the side to move is mated). Ignores the 50-move rule.
// False if pos has castling rights, more than max_pieces() pieces, or the
// prober cannot answer.
bool probe_dtm(const BB* const pos, int& result, int& plies);

} // namespace gaviota

#include "../src/gaviota.cpp"

#endif // GAVIOTA_HPP
