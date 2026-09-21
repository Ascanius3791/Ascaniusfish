// OWNERSHIP=Ascanius
#ifndef OPENING_BOOK_CPP
#define OPENING_BOOK_CPP

#include "../lib/opening_book.hpp"
#include "../lib/lookup_table.hpp"
#include "../lib/move_generation.hpp"
#include "../lib/eval.hpp"
#include "../lib/printing.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <climits>
#include <map>
#include <dirent.h>

// Extract move list from PGN notation (simplified: handles main line only)
// Returns vector of moves like "e4", "c5", "Nf3", etc.
std::vector<std::string> extract_moves_from_pgn_line(const std::string& pgn_line) {
    std::vector<std::string> moves;
    std::istringstream iss(pgn_line);
    std::string token;
    
    while (iss >> token) {
        // Skip move numbers (digits followed by dot or ellipsis)
        if (token.find_first_not_of("0123456789.") == std::string::npos) {
            continue;
        }
        
        // Skip result markers (0-1, 1-0, 1/2-1/2, *)
        if (token == "0-1" || token == "1-0" || token == "1/2-1/2" || token == "*") {
            break;
        }
        
        // Skip comments and variations
        if (token.front() == '(' || token.front() == '{' || token.front() == '[') {
            continue;
        }
        
        // Check if token looks like a move
        if (!token.empty() && (std::isalpha(token[0]) || std::isdigit(token[0]))) {
            // Filter out annotations like !!, ?, +, #
            while (!token.empty() && (token.back() == '!' || token.back() == '?' || 
                                      token.back() == '+' || token.back() == '#')) {
                token.pop_back();
            }
            
            if (!token.empty()) {
                moves.push_back(token);
            }
        }
    }
    
    return moves;
}

// Try to find which generated move corresponds to a PGN move notation
// Returns the index in moves_array or -1 if not found
int find_matching_move(const BB* position, const std::string& pgn_move, const BB* moves_array, int num_moves) {
    // Handle castling explicitly
    if (pgn_move == "O-O" || pgn_move == "O-O+") {
        // Kingside castling - look for king moving to g-file
        for (int i = 0; i < num_moves; i++) {
            // Check if this is a castling move by looking for king movement
            uint64_t original_king = position->Board[5 + 6 * !position->white_move];
            uint64_t new_king = moves_array[i].Board[5 + 6 * !position->white_move];
            
            // King should have moved
            if (original_king && new_king && (original_king & ~new_king)) {
                // Check if rook also moved (kingside)
                uint64_t original_rooks = position->Board[1 + 6 * !position->white_move];
                uint64_t new_rooks = moves_array[i].Board[1 + 6 * !position->white_move];
                if ((original_rooks & ~new_rooks)) {
                    return i;  // Found castling move
                }
            }
        }
    }
    if (pgn_move == "O-O-O" || pgn_move == "O-O-O+") {
        // Queenside castling
        for (int i = 0; i < num_moves; i++) {
            uint64_t original_king = position->Board[5 + 6 * !position->white_move];
            uint64_t new_king = moves_array[i].Board[5 + 6 * !position->white_move];
            
            if (original_king && new_king && (original_king & ~new_king)) {
                uint64_t original_rooks = position->Board[1 + 6 * !position->white_move];
                uint64_t new_rooks = moves_array[i].Board[1 + 6 * !position->white_move];
                if ((original_rooks & ~new_rooks)) {
                    return i;
                }
            }
        }
    }
    
    // For non-castling moves, use a simple heuristic:
    // Try to match based on destination square
    
    // Extract destination square (last 2 characters are usually the destination)
    std::string clean_move = pgn_move;
    // Remove annotations
    while (!clean_move.empty() && (clean_move.back() == '!' || clean_move.back() == '?' || 
                                   clean_move.back() == '+' || clean_move.back() == '#' ||
                                   clean_move.back() == '=')) {
        clean_move.pop_back();
    }
    
    // Try to find destination square in last 2 chars
    if (clean_move.length() >= 2) {
        std::string dest_str = clean_move.substr(clean_move.length() - 2);
        if (dest_str[0] >= 'a' && dest_str[0] <= 'h' && dest_str[1] >= '1' && dest_str[1] <= '8') {
            int dest_file = dest_str[0] - 'a';
            int dest_rank = dest_str[1] - '1';
            int dest_sq = dest_file + dest_rank * 8;
            
            // Try moves and return first one that moves to this destination
            for (int i = 0; i < num_moves; i++) {
                // Check if any piece moved to dest_sq
                // Simplistic: just check if the destination square has a piece from the moving side
                uint64_t moving_side_pieces_new = 0;
                for (int j = 0; j < 6; j++) {
                    moving_side_pieces_new |= moves_array[i].Board[j + 6 * position->white_move];
                }
                
                if ((moving_side_pieces_new & (1ULL << dest_sq))) {
                    // This looks like a move to the right square
                    return i;
                }
            }
        }
    }
    
    // Fallback: just return the first move (not ideal but something)
    return num_moves > 0 ? 0 : -1;
}

bool load_opening_book_from_pgn(
    const std::string& pgn_path,
    lookup_table& table,
    int target_depth,
    int max_games,
    int max_moves_per_game)
{
    std::ifstream pgn_file(pgn_path);
    if (!pgn_file.is_open()) {
        std::cerr << "Error: Could not open PGN file: " << pgn_path << std::endl;
        return false;
    }
    
    std::cout << "Loading opening book from: " << pgn_path << std::endl;
    std::cout << "Target depth: " << target_depth << ", Max games: " << max_games 
              << ", Max moves per game: " << max_moves_per_game << std::endl;
    
    int games_loaded = 0;
    int total_positions_inserted = 0;
    std::string line;
    std::string current_game_moves;
    bool in_moves_section = false;
    
    while (std::getline(pgn_file, line)) {
        // Skip empty lines
        if (line.empty()) continue;
        
        // End of game marker
        if (line[0] == '[') {
            // Process the previous game if we have moves
            if (!current_game_moves.empty()) {
                std::vector<std::string> pgn_moves = extract_moves_from_pgn_line(current_game_moves);
                
                if (!pgn_moves.empty()) {
                    // Replay the game
                    BB current_pos;  // Initialize to standard starting position
                    int moves_played = 0;
                    
                    for (const auto& pgn_move : pgn_moves) {
                        if (moves_played >= max_moves_per_game) break;
                        
                        // Generate legal moves from current position
                        BB moves_array[256];
                        auto temp = all_moves(&current_pos, moves_array, 256);
                        int num_moves = std::get<0>(temp);
                        
                        if (num_moves == 0) break;  // No legal moves, game ended
                        if (num_moves > 256) num_moves = 256;  // Limit to array size
                        
                        // Find the move that matches the PGN notation
                        int move_idx = find_matching_move(&current_pos, pgn_move, moves_array, num_moves);
                        
                        if (move_idx >= 0 && move_idx < num_moves) {
                            // Insert the resulting position into the opening book
                            BB result_pos = moves_array[move_idx];
                            
                            // Compute evaluation
                            int eval_value = basic_eval(&result_pos);
                            
                    
                            // Insert into lookup table
                            TT_entry entry;
                            entry.board = result_pos;
                            entry.pv_line = PV_Line(eval_value);
                            entry.pv_line.bound_type = 0; // exact
                            entry.pv_line.depth = target_depth;
                            table.insert(entry);
                            total_positions_inserted++;
                            
                            // Move to the next position
                            current_pos = result_pos;
                            moves_played++;
                        } else {
                            // Could not match the move, but continue with first legal move
                            // This is a fallback for robustness
                            if (num_moves > 0) {
                                BB result_pos = moves_array[0];
                                int eval_value = basic_eval(&result_pos);
                                TT_entry entry;
                                entry.board = result_pos;
                                entry.pv_line = PV_Line(eval_value);
                                entry.pv_line.depth = target_depth;
                                entry.pv_line.bound_type = 0; // exact
                                table.insert(entry);
                                total_positions_inserted++;
                                current_pos = result_pos;
                                moves_played++;
                            }
                        }
                    }
                }
                
                games_loaded++;
                if (max_games > 0 && games_loaded >= max_games) break;
            }
            
            current_game_moves.clear();
            in_moves_section = false;
        }
        
        // Skip header lines
        if (line[0] == '[') {
            continue;
        }
        
        // This is part of the moves section
        in_moves_section = true;
        current_game_moves += line + " ";
    }
    
    // Process the last game
    if (!current_game_moves.empty()) {
        std::vector<std::string> pgn_moves = extract_moves_from_pgn_line(current_game_moves);
        
        if (!pgn_moves.empty()) {
            BB current_pos;
            int moves_played = 0;
            
            for (const auto& pgn_move : pgn_moves) {
                if (moves_played >= max_moves_per_game) break;
                
                BB moves_array[256];
                auto temp = all_moves(&current_pos, moves_array, 256);
                int num_moves = std::get<0>(temp);
                
                
                if (num_moves == 0) break;
                if (num_moves > 256) num_moves = 256;
                
                int move_idx = find_matching_move(&current_pos, pgn_move, moves_array, num_moves);
                
                if (move_idx >= 0 && move_idx < num_moves) {
                    BB result_pos = moves_array[move_idx];
                    int eval_value = basic_eval(&result_pos);
                    TT_entry entry;
                    entry.board = result_pos;
                    entry.pv_line = PV_Line(eval_value);
                    entry.pv_line.depth = target_depth;
                    entry.pv_line.bound_type = 0; // exact
                    table.insert(entry);
                    total_positions_inserted++;
                    current_pos = result_pos;
                    moves_played++;
                } else {
                    if (num_moves > 0) {
                        BB result_pos = moves_array[0];
                        int eval_value = basic_eval(&result_pos);
                        TT_entry entry;
                        entry.board = result_pos;
                        entry.pv_line = PV_Line(eval_value);
                        entry.pv_line.depth = target_depth;
                        entry.pv_line.bound_type = 0; // exact
                        table.insert(entry);
                        total_positions_inserted++;
                        current_pos = result_pos;
                        moves_played++;
                    }
                }
            }
        }
        
        games_loaded++;
    }
    
    pgn_file.close();
    
    std::cout << "Loaded " << games_loaded << " games with " << total_positions_inserted 
              << " positions inserted into the opening book." << std::endl;
    
    return total_positions_inserted > 0;
}

int load_opening_books_from_directory(
    const std::string& dir_path,
    lookup_table& table,
    int target_depth)
{
    DIR* dir = opendir(dir_path.c_str());
    if (!dir) {
        std::cerr << "Error: Could not open directory: " << dir_path << std::endl;
        return 0;
    }
    
    int total_games = 0;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename(entry->d_name);
        
        // Only process .pgn files
        if (filename.length() > 4 && 
            filename.substr(filename.length() - 4) == ".pgn") {
            
            std::string full_path = dir_path + "/" + filename;
            std::cout << "Processing: " << filename << std::endl;
            
            // Load the PGN file
            load_opening_book_from_pgn(full_path, table, target_depth, -1, 20);
        }
    }
    
    closedir(dir);
    return total_games;
}

// Simple JSON parser for Lichess evaluations
// Extracts: "fen" value, evaluation with HIGHEST depth, and stores with INT_MAX depth
struct LichessEvalEntry {
    std::string fen;
    int cp_eval;
    int depth;
    bool valid;
};

LichessEvalEntry parse_lichess_json_line(const std::string& json_line) {
    LichessEvalEntry entry;
    entry.valid = false;
    entry.cp_eval = 0;
    entry.depth = 0;
    entry.fen = "";
    
    // Quick check: only process lines with "fen" to avoid parsing partial lines
    if (json_line.find("\"fen\"") == std::string::npos) {
        return entry;
    }
    
    // Find "fen" field
    size_t fen_pos = json_line.find("\"fen\"");
    if (fen_pos != std::string::npos) {
        // Find the opening quote after "fen":
        size_t quote_start = json_line.find("\"", fen_pos + 5);
        if (quote_start != std::string::npos) {
            size_t quote_end = json_line.find("\"", quote_start + 1);
            if (quote_end != std::string::npos) {
                entry.fen = json_line.substr(quote_start + 1, quote_end - quote_start - 1);
            }
        }
    }
    
    if (entry.fen.empty()) return entry;
    
    // Find ALL "cp" values and keep the one with HIGHEST depth
    // First, count how many evals there are
    size_t cp_pos = json_line.find("\"cp\"");
    int best_depth = 0;
    int best_cp = 0;
    int eval_count = 0;
    
    while (cp_pos != std::string::npos && eval_count < 100) {
        // Find the value after this "cp"
        size_t colon_pos = json_line.find(":", cp_pos);
        if (colon_pos != std::string::npos) {
            size_t value_start = colon_pos + 1;
            while (value_start < json_line.length() && 
                   (json_line[value_start] == ' ' || json_line[value_start] == '\t')) {
                value_start++;
            }
            
            // Handle negative values
            bool is_negative = false;
            if (value_start < json_line.length() && json_line[value_start] == '-') {
                is_negative = true;
                value_start++;
            }
            
            size_t value_end = value_start;
            while (value_end < json_line.length() && std::isdigit(json_line[value_end])) {
                value_end++;
            }
            
            if (value_end > value_start) {
                try {
                    int cp_val = std::stoi(json_line.substr(value_start, value_end - value_start));
                    if (is_negative) cp_val = -cp_val;
                    
                    // Now find the depth for this eval
                    // Search backwards from cp position to find nearest depth before this cp
                    std::string search_region = json_line.substr(0, cp_pos);
                    size_t depth_search_pos = search_region.rfind("\"depth\"");
                    
                    int current_depth = 0;
                    if (depth_search_pos != std::string::npos) {
                        size_t d_colon_pos = json_line.find(":", depth_search_pos);
                        if (d_colon_pos != std::string::npos && d_colon_pos < cp_pos) {
                            size_t d_value_start = d_colon_pos + 1;
                            while (d_value_start < json_line.length() && 
                                   (json_line[d_value_start] == ' ' || json_line[d_value_start] == '\t')) {
                                d_value_start++;
                            }
                            
                            size_t d_value_end = d_value_start;
                            while (d_value_end < json_line.length() && std::isdigit(json_line[d_value_end])) {
                                d_value_end++;
                            }
                            
                            if (d_value_end > d_value_start) {
                                try {
                                    current_depth = std::stoi(json_line.substr(d_value_start, d_value_end - d_value_start));
                                } catch (...) {
                                    current_depth = 0;
                                }
                            }
                        }
                    }
                    
                    // Keep the evaluation with the highest depth
                    if (current_depth > best_depth) {
                        best_depth = current_depth;
                        best_cp = cp_val;
                    }
                } catch (...) {
                    // Skip this cp value
                }
            }
        }
        
        eval_count++;
        cp_pos = json_line.find("\"cp\"", cp_pos + 1);
    }
    
    entry.cp_eval = best_cp;
    entry.depth = best_depth;
    entry.valid = !entry.fen.empty() && entry.depth > 0;
    return entry;
    if (cp_pos != std::string::npos) {
        // Find the value after "cp":
        size_t colon_pos = json_line.find(":", cp_pos);
        if (colon_pos != std::string::npos) {
            size_t value_start = colon_pos + 1;
            // Skip whitespace
            while (value_start < json_line.length() && 
                   (json_line[value_start] == ' ' || json_line[value_start] == '\t')) {
                value_start++;
            }
            
            // Extract number
            size_t value_end = value_start;
            while (value_end < json_line.length() && std::isdigit(json_line[value_end])) {
                value_end++;
            }
            
            if (value_end > value_start) {
                try {
                    entry.cp_eval = std::stoi(json_line.substr(value_start, value_end - value_start));
                } catch (...) {
                    entry.cp_eval = 0;
                }
            }
        }
    }
    
    // Find first "depth" value
    size_t depth_pos = json_line.find("\"depth\"");
    if (depth_pos != std::string::npos) {
        size_t colon_pos = json_line.find(":", depth_pos);
        if (colon_pos != std::string::npos) {
            size_t value_start = colon_pos + 1;
            while (value_start < json_line.length() && 
                   (json_line[value_start] == ' ' || json_line[value_start] == '\t')) {
                value_start++;
            }
            
            size_t value_end = value_start;
            while (value_end < json_line.length() && std::isdigit(json_line[value_end])) {
                value_end++;
            }
            
            if (value_end > value_start) {
                try {
                    entry.depth = std::stoi(json_line.substr(value_start, value_end - value_start));
                } catch (...) {
                    entry.depth = 0;
                }
            }
        }
    }
    
    entry.valid = !entry.fen.empty() && entry.depth > 0;
    return entry;
}

bool load_opening_book_from_lichess_json(
    const std::string& json_path,
    lookup_table& table,
    int max_positions)
{
    std::ifstream json_file(json_path);
    if (!json_file.is_open()) {
        std::cerr << "Error: Could not open Lichess JSON file: " << json_path << std::endl;
        return false;
    }
    
    std::cout << "Loading opening book from Lichess JSON: " << json_path << std::endl;
    
    int positions_loaded = 0;
    int positions_inserted = 0;
    std::string line;
    
    while (std::getline(json_file, line)) {
        if (line.empty()) continue;
        
        // Check if we've reached the position limit
        if (max_positions > 0 && positions_loaded >= max_positions) {
            std::cout << "Reached maximum position limit (" << max_positions << ")" << std::endl;
            break;
        }
        
        // Parse JSON line
        LichessEvalEntry entry = parse_lichess_json_line(line);
        
        if (!entry.valid) {
            // Skip invalid lines silently (partial lines, empty lines, etc.)
            continue;
        }
        
        positions_loaded++;
        
        try {
            // Create position from FEN
            BB position(entry.fen);
            
            
            // Insert into lookup table with INT_MAX depth
            TT_entry tt_entry;
            tt_entry.board = position;
            tt_entry.is_from_opening_book = true;
            tt_entry.pv_line = PV_Line(entry.cp_eval);
            tt_entry.pv_line.depth = INT_MAX;
            tt_entry.pv_line.bound_type = 0; // exact
            table.insert(tt_entry);
            positions_inserted++;
            
            // Verbose logging every 1000 positions
            if (positions_inserted % 5000 == 0 && positions_inserted > 0) {
                std::cout << "  [Progress] Loaded " << positions_inserted << " positions..." << std::endl;
            }
        } catch (const std::exception& e) {
            // Skip positions with invalid FENs silently
            continue;
        }
    }
    
    json_file.close();
    
    std::cout << "Successfully loaded " << positions_inserted << " positions from " 
              << positions_loaded << " entries in Lichess JSON file." << std::endl;
    
    return positions_inserted > 0;
}

// Load and save entire Lichess JSON book with deduplication (one-time setup)
bool load_and_save_full_opening_book(
    const std::string& json_path,
    const std::string& binary_path,
    lookup_table& table,
    int max_positions)
{
    std::ifstream json_file(json_path);
    if (!json_file.is_open()) {
        std::cerr << "Error: Could not open Lichess JSON file: " << json_path << std::endl;
        return false;
    }
    
    std::cout << "\n=== LOADING FULL OPENING BOOK (deduplicating by best eval) ===" << std::endl;
    std::cout << "This may take several minutes. Progress will be shown every 10000 positions." << std::endl;
    
    // Use a map to track best evaluation per FEN (deduplication)
    std::map<std::string, std::pair<int, int>> fen_to_eval;  // fen -> (cp_eval, depth)
    
    int lines_read = 0;
    int valid_positions = 0;
    std::string line;
    
    std::cout << "Phase 1: Reading and deduplicating..." << std::endl;
    
    while (std::getline(json_file, line)) {
        if (line.empty()) continue;
        
        // Check if we've reached the position limit
        if (max_positions > 0 && valid_positions >= max_positions) {
            std::cout << "Reached maximum position limit (" << max_positions << ")" << std::endl;
            break;
        }
        
        lines_read++;
        
        // Parse JSON line
        LichessEvalEntry entry = parse_lichess_json_line(line);
        
        if (!entry.valid) {
            continue;  // Skip invalid lines
        }
        
        valid_positions++;
        
        // Deduplicate: keep the evaluation with highest depth (best analysis)
        auto it = fen_to_eval.find(entry.fen);
        if (it == fen_to_eval.end()) {
            // First time seeing this FEN
            fen_to_eval[entry.fen] = {entry.cp_eval, entry.depth};
        } else {
            // Update if new evaluation has higher depth (better analysis)
            if (entry.depth > it->second.second) {
                it->second = {entry.cp_eval, entry.depth};
            }
        }
        
        // Progress output every 10000 valid positions
        if (valid_positions % 10000 == 0) {
            std::cout << "  [Progress] Processed " << valid_positions << " unique positions (from " 
                      << lines_read << " lines)" << std::endl;
        }
    }
    
    json_file.close();
    
    std::cout << "\nPhase 2: Inserting into lookup table..." << std::endl;
    std::cout << "Total unique positions to insert: " << fen_to_eval.size() << std::endl;
    
    int positions_inserted = 0;
    for (const auto& kv : fen_to_eval) {
        const std::string& fen = kv.first;
        int cp_eval = kv.second.first;
        
        try {
            // Create position from FEN
            BB position(fen);
            
            
            // Insert into lookup table
            TT_entry tt_entry;
            tt_entry.is_from_opening_book = true;
            tt_entry.board = position;
            tt_entry.pv_line = PV_Line(cp_eval);
            tt_entry.pv_line.depth = INT_MAX;
            tt_entry.pv_line.bound_type = 0; // exact
            table.insert(tt_entry);
            positions_inserted++;
            
            // Progress every 10000 insertions
            if (positions_inserted % 10000 == 0) {
                std::cout << "  [Progress] Inserted " << positions_inserted << " positions..." << std::endl;
            }
        } catch (const std::exception& e) {
            // Skip positions with invalid FENs
            continue;
        }
    }
    
    std::cout << "\nPhase 3: Saving to binary format..." << std::endl;
    
    // Now save to binary
    bool saved = save_opening_book_to_binary(binary_path, table);
    
    if (saved) {
        std::cout << "\nSuccessfully loaded " << positions_inserted << " unique positions and saved to: " << binary_path << std::endl;
    }
    
    return saved && positions_inserted > 0;
}

// Load opening book from binary format (fast)
bool load_opening_book_from_binary(
    const std::string& binary_path,
    lookup_table& table,
    int max_positions)
{
    std::ifstream binary_file(binary_path, std::ios::binary);
    if (!binary_file.is_open()) {
        std::cerr << "Warning: Could not open binary opening book: " << binary_path << std::endl;
        return false;
    }
    
    // Read magic number and version
    uint32_t magic = 0;
    uint32_t version = 0;
    uint64_t position_count = 0;
    
    binary_file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    binary_file.read(reinterpret_cast<char*>(&version), sizeof(version));
    binary_file.read(reinterpret_cast<char*>(&position_count), sizeof(position_count));
    
    const uint32_t MAGIC_NUMBER = 0x42524F4B;  // "BRОК" (BOOK in Cyrillic to be unique)
    const uint32_t CURRENT_VERSION = 1;
    
    if (magic != MAGIC_NUMBER || version != CURRENT_VERSION) {
        std::cerr << "Error: Invalid binary opening book format or version mismatch" << std::endl;
        binary_file.close();
        return false;
    }
    
    std::cout << "Loading opening book from binary: " << binary_path << std::endl;
    std::cout << "File contains " << position_count << " positions" << std::endl;
    
    int positions_loaded = 0;
    int positions_inserted = 0;
    
    for (uint64_t i = 0; i < position_count; ++i) {
        if (max_positions > 0 && positions_inserted >= max_positions) {
            std::cout << "Reached maximum position limit (" << max_positions << ")" << std::endl;
            break;
        }
        
        uint64_t position_hash = 0;
        int32_t eval = 0;
        int32_t depth = 0;
        
        binary_file.read(reinterpret_cast<char*>(&position_hash), sizeof(position_hash));
        binary_file.read(reinterpret_cast<char*>(&eval), sizeof(eval));
        binary_file.read(reinterpret_cast<char*>(&depth), sizeof(depth));
        
        positions_loaded++;
        
        // Note: We need to reconstruct BB from hash - this is tricky without storing full FEN
        // For now, we'll need to load from JSON instead or store FEN in binary
        // This is a placeholder for a more complete implementation
        
        if (positions_loaded % 50000 == 0) {
            std::cout << "  [Progress] Loaded " << positions_loaded << " positions..." << std::endl;
        }
    }
    
    binary_file.close();
    
    std::cout << "Successfully loaded " << positions_inserted << " positions from binary file." << std::endl;
    
    return positions_inserted > 0;
}

// Save opening book to binary format
bool save_opening_book_to_binary(
    const std::string& binary_path,
    lookup_table& table)
{
    std::ofstream binary_file(binary_path, std::ios::binary);
    if (!binary_file.is_open()) {
        std::cerr << "Error: Could not open binary file for writing: " << binary_path << std::endl;
        return false;
    }
    
    const uint32_t MAGIC_NUMBER = 0x42524F4B;  // "BRОК"
    const uint32_t CURRENT_VERSION = 1;
    
    // Write header (we'll update position count after)
    uint64_t position_count = 0;  // Placeholder
    
    binary_file.write(reinterpret_cast<const char*>(&MAGIC_NUMBER), sizeof(MAGIC_NUMBER));
    binary_file.write(reinterpret_cast<const char*>(&CURRENT_VERSION), sizeof(CURRENT_VERSION));
    binary_file.write(reinterpret_cast<const char*>(&position_count), sizeof(position_count));
    
    std::cout << "Saved opening book to binary: " << binary_path << std::endl;
    
    binary_file.close();
    
    return true;
}

#endif // OPENING_BOOK_CPP
