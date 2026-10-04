// OWNERSHIP=Claude
#ifndef WEIGHT_SET_HPP
#define WEIGHT_SET_HPP
#include "Weights.hpp"
#include <string>
#include "weights_default.hpp"  // WEIGHTS_DEFAULT_TEXT: the default set, generated from WEIGHTS_DEFAULT's file

// A weight set (#85): every number basic_eval() uses, as a text file that names
// its version. weights/w1.txt is set 1; the default set (WEIGHTS_OG's values, set 4
// since #90) is compiled in from lib/weights_default.hpp, which the Makefile
// generates from the file WEIGHTS_DEFAULT names, so a new default is a new file
// and not an edit of src/Weights.cpp. UCI
// `WeightsFile` loads another one, and the engine reports "weights N".
//
// The format is "key values...", whitespace-separated, '#' to the end of a line
// a comment:
//   version N          required, N >= 1
//   parent N           the set it was tuned from (0 = none)
//   data D, tuner T, loss L   one word each, "-" = none: the position set,
//                      the tuner's commit and its fit loss
//   <field> v...       one per WEIGHTS field basic_eval() reads, named as in
//                      lib/Weights.hpp, with as many numbers as it has
//   piece_table_value_{opening,endgame} <piece> 64 numbers
//                      piece = pawn rook knight bishop queen king; 8 rows,
//                      rank 8 first, files a..h, white's view (black reads
//                      the table rank-flipped)
// Every field is required exactly once; an unknown key, a missing or extra
// number, or a zero divisor is an error. The fields basic_eval() does not use
// (skip_depth_decrease_threshold, value_of_attacked_square, check_value,
// offensive_value, defensive_value, king_safety_value,
// value_of_king_safety_for_sorting) are not in a set: reading one leaves them
// as they were.

struct Weight_Set_Info
{
    int parent = 0;
    std::string data = "-", tuner = "-", loss = "-";
};

// Reads `text` into W (version included). On an error W is unchanged and
// `error` says what and on which line.
bool read_weight_set(const std::string& text, WEIGHTS& W, Weight_Set_Info& info, std::string& error);
bool load_weight_set(const std::string& path, WEIGHTS& W, Weight_Set_Info& info, std::string& error);

// The text read_weight_set() reads back to the same W and info.
std::string write_weight_set(const WEIGHTS& W, const Weight_Set_Info& info);

#include "../src/weight_set.cpp"

#endif // WEIGHT_SET_HPP
