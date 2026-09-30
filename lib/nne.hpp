// OWNERSHIP=Claude
// The eval-correction net's inputs (M9, docs/NNE_DESIGN.md). This is the one
// place they are computed: the data tool (tools/nne_data.cpp) writes them into
// the dataset, and the engine's inference will call the same function, so the
// trainer never computes features itself and cannot drift from the engine.
//
// The board is seen from the side to move. With black to move the ranks are
// flipped (square ^ 56) and the colours swapped, so both sides to move are one
// kind of data and the side to move is not an input. Scores the net gives are
// in the mover's view; to_mover() turns an engine score (white-positive) into
// that view and back.
//
// Inputs (780), each 0 or 1:
//   [0, 768)    (colour*6 + piece)*64 + square: colour 0 own, 1 opponent;
//               piece in the engine's order 0 P, 1 R, 2 N, 3 B, 4 Q, 5 K;
//               square a1=0 .. h8=63 after the flip.
//   768..771    castling rights: own king side, own queen side,
//               opponent king side, opponent queen side.
//   772..779    the en-passant file, only when a pawn of the mover stands next
//               to the double-pushed pawn (a capture is possible, pins aside),
//               so a position reached by make_move() and the same one read
//               from a FEN get the same inputs.
#ifndef NNE_HPP
#define NNE_HPP

#include "Bitboards.hpp"
#include <cstdint>

namespace nne
{
constexpr int N_PIECE_INPUTS = 768;
constexpr int CASTLING_INPUT = 768;
constexpr int EN_PASSANT_INPUT = 772;
constexpr int N_INPUTS = 780;
constexpr int MAX_ACTIVE = 32 + 4 + 1;  // pieces, castling rights, en passant

// An engine score (white-positive) in the mover's view, and back: the same flip.
inline int to_mover(int score, bool white_move)
{
    return white_move ? score : -score;
}

// Writes the indices of the active inputs to `out`, ascending, and returns how
// many there are (at most MAX_ACTIVE).
inline int active_features(const BB& pos, int* out)
{
    const bool white = pos.white_move;
    const int own = white ? 0 : 6, opp = white ? 6 : 0;
    int n = 0;
    for(int side=0; side<2; side++)
    for(int piece=0; piece<6; piece++)
    {
        uint64_t b = pos.Board[(side==0 ? own : opp) + piece];
        if(!white)
        b = __builtin_bswap64(b);  // one byte per rank: swapping bytes flips the ranks
        const int base = (side*6 + piece)*64;
        while(b)
        {
            out[n++] = base + __builtin_ctzll(b);
            b &= b-1;
        }
    }
    // castle[colour][side]: colour 1 white, 0 black; side 1 king side, 0 queen side
    const int us = white ? 1 : 0, them = 1-us;
    if(pos.castle[us][1])   out[n++] = CASTLING_INPUT + 0;
    if(pos.castle[us][0])   out[n++] = CASTLING_INPUT + 1;
    if(pos.castle[them][1]) out[n++] = CASTLING_INPUT + 2;
    if(pos.castle[them][0]) out[n++] = CASTLING_INPUT + 3;

    const uint64_t ep = pos.en_passant;
    if(ep)
    {
        const uint64_t FILE_A = 0x0101010101010101ULL, FILE_H = FILE_A << 7;
        // The squares a capturing pawn of the mover would stand on.
        const uint64_t from = white ? ((ep >> 7) & ~FILE_A) | ((ep >> 9) & ~FILE_H)
                                    : ((ep << 7) & ~FILE_H) | ((ep << 9) & ~FILE_A);
        if(from & pos.Board[own])
        out[n++] = EN_PASSANT_INPUT + __builtin_ctzll(ep) % 8;
    }
    return n;
}
}

#endif // NNE_HPP
