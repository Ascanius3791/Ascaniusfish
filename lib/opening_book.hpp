// OWNERSHIP=Ascanius
#ifndef OPENING_BOOK_HPP
#define OPENING_BOOK_HPP

#include <string>
#include <vector>
#include "../lib/Bitboards.hpp"
#include "../lib/lookup_table.hpp"

// Load positions from a PGN file into the lookup table
// Returns true on success, false on error
bool load_opening_book_from_pgn(
    const std::string& pgn_path,
    lookup_table& table,
    int target_depth = 100,
    int max_games = -1,
    int max_moves_per_game = 20
);

// Load positions from a directory of PGN files
// Returns number of games loaded
int load_opening_books_from_directory(
    const std::string& dir_path,
    lookup_table& table,
    int target_depth = 100
);

// Load positions from Lichess JSON evaluation file (one JSON object per line)
// Format: {"fen": "...", "evals": [{"pvs": [{"cp": 123, ...}], "depth": 36, ...}]}
// Returns true on success, false on error
bool load_opening_book_from_lichess_json(
    const std::string& json_path,
    lookup_table& table,
    int max_positions = -1
);

// Load entire Lichess JSON book (with deduplication to keep best eval per position)
// and save as binary for fast loading next time
// Returns true on success, false on error
bool load_and_save_full_opening_book(
    const std::string& json_path,
    const std::string& binary_path,
    lookup_table& table,
    int max_positions = -1
);

// Load opening book from binary format (fast)
// Returns true on success, false on error
bool load_opening_book_from_binary(
    const std::string& binary_path,
    lookup_table& table,
    int max_positions = -1
);

// Save opening book to binary format
// Returns true on success, false on error
bool save_opening_book_to_binary(
    const std::string& binary_path,
    lookup_table& table
);

#include "../src/opening_book.cpp"

#endif // OPENING_BOOK_HPP
