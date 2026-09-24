// OWNERSHIP=Ascanius
#ifndef ZOBRIST_HPP
#define ZOBRIST_HPP

#include <cstdint>
#include <random>
#include "../src/Bitboards.cpp"

class Zobrist
{
public:
    inline static uint64_t pieceKeys[12][64];
    inline static uint64_t sideKey;
    inline static uint64_t castlingKeys[16];
    inline static uint64_t enPassantKeys[8];

    Zobrist();
    static uint64_t compute_Zobrist_Hash(const BB& board);
    private:
    static int castling_rights_to_index(const BB& board);
    static int en_passant_to_index(const BB& board);

    static inline void adjust_for_side_to_move(BB& new_board);

    static void update_metadata_part_of_zobrist_hash(const BB& old_board, BB& new_board);
    static void update_standart_move_of_zobrist_hash(const BB& old_board, BB& new_board, int piece_type, int from, int to, bool is_capture, int promotion_piece_type = -1, bool is_en_passant_capture = false);
    public:
    static void update_zobrist_hash(const BB& old_board, BB& new_board, int piece_type, int from, int to, bool is_capture, int promotion_piece_type = -1, bool is_en_passant_capture = false);
    static void update_zobrist_hash_castling_piece_position(const BB& old_board, BB& new_board, bool King_side);
    // For a null move: no piece moves and castling rights are unchanged, so
    // only the side-to-move key and (if one was set) the old en-passant file
    // key need to come out of the hash - unlike update_zobrist_hash*, there's
    // no standard-move or castling-rights part to update here.
    static void update_zobrist_hash_null_move(const BB& old_board, BB& new_board);

    static bool has_correct_zobrist_hash(const BB& board);
};




#endif // ZOBRIST_HPP
