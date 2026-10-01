// OWNERSHIP=Claude
// The eval-correction net (M9, docs/NNE_DESIGN.md): its inputs, and the
// engine's inference (#52). The inputs are computed only here: the data tool
// (tools/nne_data.cpp) writes them into the dataset and the inference calls
// the same function, so the trainer never computes features itself and cannot
// drift from the engine.
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
#include "tb_search.hpp"
#include <cstdint>
#include <string>

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

// The inputs as a set of bits: input i is bit i%64 of word i/64. Words 0-11 are
// the piece bitboards themselves (seen from the mover), word 12 the castling
// rights and the en-passant file, so two positions' inputs differ exactly where
// their words do (what the engine's first layer is updated by, #55).
constexpr int FEATURE_WORDS = 13;

inline void feature_set(const BB& pos, uint64_t* words)
{
    const bool white = pos.white_move;
    const int own = white ? 0 : 6, opp = white ? 6 : 0;
    for(int piece=0; piece<6; piece++)
    {
        uint64_t o = pos.Board[own + piece], t = pos.Board[opp + piece];
        if(!white)
        {
            o = __builtin_bswap64(o);  // one byte per rank: swapping bytes flips the ranks
            t = __builtin_bswap64(t);
        }
        words[piece] = o;
        words[6 + piece] = t;
    }
    // castle[colour][side]: colour 1 white, 0 black; side 1 king side, 0 queen side
    const int us = white ? 1 : 0, them = 1-us;
    uint64_t extra = uint64_t(pos.castle[us][1]   ? 1 : 0) << (CASTLING_INPUT + 0 - N_PIECE_INPUTS)
                   | uint64_t(pos.castle[us][0]   ? 1 : 0) << (CASTLING_INPUT + 1 - N_PIECE_INPUTS)
                   | uint64_t(pos.castle[them][1] ? 1 : 0) << (CASTLING_INPUT + 2 - N_PIECE_INPUTS)
                   | uint64_t(pos.castle[them][0] ? 1 : 0) << (CASTLING_INPUT + 3 - N_PIECE_INPUTS);

    const uint64_t ep = pos.en_passant;
    if(ep)
    {
        const uint64_t FILE_A = 0x0101010101010101ULL, FILE_H = FILE_A << 7;
        // The squares a capturing pawn of the mover would stand on.
        const uint64_t from = white ? ((ep >> 7) & ~FILE_A) | ((ep >> 9) & ~FILE_H)
                                    : ((ep << 7) & ~FILE_H) | ((ep << 9) & ~FILE_A);
        if(from & pos.Board[own])
        extra |= uint64_t(1) << (EN_PASSANT_INPUT - N_PIECE_INPUTS + __builtin_ctzll(ep) % 8);
    }
    words[12] = extra;
}

// Writes the indices of the active inputs to `out`, ascending, and returns how
// many there are (at most MAX_ACTIVE).
inline int active_features(const BB& pos, int* out)
{
    uint64_t words[FEATURE_WORDS];
    feature_set(pos, words);
    int n = 0;
    for(int w=0; w<FEATURE_WORDS; w++)
    for(uint64_t b = words[w]; b; b &= b-1)
    out[n++] = 64*w + __builtin_ctzll(b);
    return n;
}

// The net, 780 -> 128 -> 16 -> 1 with a clipped ReLU (clamp to [0, 1]) after
// both hidden layers, float32 in the file and the trainer. Weights are [in][out],
// so an active input is one contiguous row of W1. The last layer is already in cp:
//   c = b3 + clamp(b2 + clamp(b1 + sum of W1[active], 0, 1) W2, 0, 1) W3
constexpr int HIDDEN_1 = 128;
constexpr int HIDDEN_2 = 16;

struct Net
{
    alignas(64) float w1[N_INPUTS*HIDDEN_1];
    alignas(64) float b1[HIDDEN_1];
    alignas(64) float w2[HIDDEN_1*HIDDEN_2];
    alignas(64) float b2[HIDDEN_2];
    float w3[HIDDEN_2];
    float b3;
};

// What the engine computes with (#55), integers up to layer 2's sums, with
// every scale chosen at load time from the file's weights:
// - Layer 1 is int16: unit j's weights and bias are scaled by scale[j] and
//   rounded, scale[j] as large as it can be while no set of inputs can take the
//   unit's sum out of int16 (|b1| plus its 37 largest |W1|, times the scale,
//   stays under 32768). The clipped ReLU is then clamp(sum, 0, scale[j]).
// - Layer 2 is int16 times int16 into int32: W2[j][k] / scale[j] * w2_scale,
//   rounded, with w2_scale as large as it can be while no h1 can take a sum
//   out of int32 and no weight out of int16. Weights are stored by pairs of
//   units, [j/2][k][j%2], as pmaddwd multiplies and adds them.
// - b2, layer 2's clipped ReLU and layer 3 are float, in one fixed order.
// Integer sums are exact, so they are the same whatever order or SIMD width
// they are added in: a first layer can be updated from another position's
// instead of recomputed, and every machine gets the same bits.
struct Quantized_Net
{
    alignas(64) int16_t w1[N_INPUTS*HIDDEN_1];
    alignas(64) int16_t b1[HIDDEN_1];
    alignas(64) int16_t scale[HIDDEN_1];
    alignas(64) int16_t w2[HIDDEN_1*HIDDEN_2];  // [j/2][k][j%2]
    float w2_scale;
    alignas(64) float b2[HIDDEN_2];
    float w3[HIDDEN_2];
    float b3;
};

inline Quantized_Net qnet;    // load()'s file, quantized; zero until then
inline bool enabled = false;  // UseNNE: the quiet leaf of minimax_tactical() adds the net's correction
inline std::string loaded_path;  // the file in `qnet`, empty if none
inline bool use_avx2 = false;    // set by load() from the CPU; both paths give identical results

// Reads a weights file ("NNE1", version 1, 3 layers, sizes 780 128 16 1, then
// per layer float32 W [in][out] and b [out], little-endian) and quantizes it
// into `qnet`. On any error `qnet` and loaded_path are left as they were and
// `error` says why.
bool load(const std::string& path, std::string& error);

// The net's correction for `pos` in centipawns, in the mover's view. Needs a
// loaded net. The first layer's sums are kept per thread and side to move and
// updated from the last position's by the inputs that differ, so a leaf next to
// the previous one costs a few rows of W1 rather than all of them.
float correction(const BB& pos);
// The same, with the first layer summed from scratch (for tests: it must give
// exactly what correction() does).
float correction_from_scratch(const BB& pos);

// The quiet-leaf score: `static_eval` (white's view, from eval()) plus the net's
// correction, rounded and clamped to +-SCORE_LIMIT, so however large the static
// eval is, the sum never reaches the TB band (|eval| >= TB_WIN_SCORE) or the
// mate band beyond it.
constexpr int SCORE_LIMIT = TB_WIN_SCORE - 1;
int corrected_eval(const BB* pos, int static_eval);
}

#include "../src/nne.cpp"

#endif // NNE_HPP
