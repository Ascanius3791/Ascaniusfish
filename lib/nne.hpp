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
// Inputs, each 0 or 1. The piece inputs (780) are every net's:
//   [0, 768)    (colour*6 + piece)*64 + square: colour 0 own, 1 opponent;
//               piece in the engine's order 0 P, 1 R, 2 N, 3 B, 4 Q, 5 K;
//               square a1=0 .. h8=63 after the flip.
//   768..771    castling rights: own king side, own queen side,
//               opponent king side, opponent queen side.
//   772..779    the en-passant file, only when a pawn of the mover stands next
//               to the double-pushed pawn (a capture is possible, pins aside),
//               so a position reached by make_move() and the same one read
//               from a FEN get the same inputs.
// A net on named features (#114) reads 3072: the piece inputs, then from word
// 13 on one bitboard per named feature and side (named_word(), the Named
// enum), and the global flags in word 47 (the Global enum). Input i is bit
// i%64 of word i/64 in both layouts, so unused bits are inputs that are never
// on.
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
constexpr int N_INPUTS_PIECES = 780;
constexpr int MAX_ACTIVE = 32 + 4 + 1;  // of the piece inputs: pieces, castling rights, en passant

// An engine score (white-positive) in the mover's view, and back: the same flip.
inline int to_mover(int score, bool white_move)
{
    return white_move ? score : -score;
}

// The inputs as a set of bits: input i is bit i%64 of word i/64. Words 0-11 are
// the piece bitboards themselves (seen from the mover), word 12 the castling
// rights and the en-passant file, so two positions' inputs differ exactly where
// their words do (what the engine's first layer is updated by, #55).
constexpr int PIECE_WORDS = 13;

// The named features (#114), each a bitboard per side: word named_word(f, 0)
// marks the mover's pieces with feature f, named_word(f, 1) the opponent's.
// The definitions follow basic_eval()'s terms where it has one.
enum Named
{
    PASSED,              // no enemy pawn ahead on its own or a neighbouring file
    PASSED_FREE,         // passed, and nothing stands ahead of it on its file
    ISOLATED,            // no own pawn on a neighbouring file
    DOUBLED,             // another own pawn on its file
    BACKWARD,            // own pawns beside it all ahead of it, and its stop square attacked by an enemy pawn
    PHALANX,             // an own pawn right beside it
    PROTECTED_PAWN,      // defended by an own pawn
    BLOCKED,             // the square ahead of it occupied
    KNIGHT_OUTPOST,      // relative rank 4-6, defended by a pawn, no enemy pawn can ever attack it
    BISHOP_OUTPOST,
    PAWN_DEFENDED,       // a knight, bishop, rook or queen defended by an own pawn
    HANGING,             // a piece but the king, attacked and not defended
    ATTACKED_BY_LESSER,  // minor by a pawn, rook by a pawn or minor, queen by a pawn, minor or rook
    ROOK_OPEN,           // a rook on a file without pawns
    ROOK_HALF_OPEN,      // a rook on a file with enemy pawns only
    KING_RING_ATTACKED,  // the squares next to the own king that the enemy attacks
    ATTACKS,             // every square the side attacks
    NAMED_FEATURES
};
// The most bits a feature's board can hold, for the int32 bound of quantize().
constexpr int NAMED_CAP[NAMED_FEATURES] = {8, 8, 8, 8, 8, 8, 8, 8, 10, 10, 15, 15, 15, 10, 10, 8, 64};

constexpr int named_word(int feature, int side) { return PIECE_WORDS + 2*feature + side; }
constexpr int GLOBAL_WORD = named_word(NAMED_FEATURES, 0);
enum Global
{
    OWN_BISHOP_PAIR, OPP_BISHOP_PAIR,
    OPPOSITE_BISHOPS,                 // one bishop each, on squares of different colours
    OWN_NO_PIECES, OPP_NO_PIECES,     // no knight, bishop, rook or queen
    OWN_UNSTOPPABLE, OPP_UNSTOPPABLE, // a passer the enemy king cannot catch, enemy without pieces (basic_eval's rule)
    PHASE_BUCKET = 8,                 // 8 bits, one on: game phase (basic_eval's, 0..24) * 8 / 25
};
constexpr int GLOBAL_CAP = 8;  // bits on at once in the global word

constexpr int FEATURE_WORDS = GLOBAL_WORD + 1;   // the most words any net reads
constexpr int N_INPUTS_NAMED = 64*FEATURE_WORDS; // 3072
constexpr int N_INPUTS = N_INPUTS_NAMED;         // the most inputs any net has

// The named features' words from the 12 piece words (mover's view, the mover's
// pawns moving up), into named[0 .. FEATURE_WORDS-PIECE_WORDS).
void named_features(const uint64_t* pieces, uint64_t* named);

// The words of the first `words` (PIECE_WORDS or FEATURE_WORDS) for `pos`.
inline void feature_set(const BB& pos, uint64_t* words, int n_words = PIECE_WORDS)
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
    if(n_words > PIECE_WORDS)
    named_features(words, words + PIECE_WORDS);
}

// Writes the indices of the active inputs to `out` (room for N_INPUTS),
// ascending, and returns how many there are.
inline int active_features(const BB& pos, int* out, int n_words = PIECE_WORDS)
{
    uint64_t words[FEATURE_WORDS];
    feature_set(pos, words, n_words);
    int n = 0;
    for(int w=0; w<n_words; w++)
    for(uint64_t b = words[w]; b; b &= b-1)
    out[n++] = 64*w + __builtin_ctzll(b);
    return n;
}

// The net: inputs -> 128 [-> 128] -> 16 -> 1, a clipped ReLU (clamp to [0, 1])
// after every hidden layer, float32 in the file and the trainer. The inputs
// are the 780 piece inputs or the 3072 with named features; the middle 128
// layer is optional (#114). Weights are [in][out], so an active input is one
// contiguous row of W1. The last layer is already in cp:
//   h1 = clamp(b1 + sum of W1[active], 0, 1)
//   [h1 = clamp(bm + h1 Wm, 0, 1)]
//   c = b3 + clamp(b2 + h1 W2, 0, 1) W3
constexpr int HIDDEN_1 = 128;
constexpr int HIDDEN_2 = 16;

struct Net
{
    alignas(64) float w1[N_INPUTS*HIDDEN_1];
    alignas(64) float b1[HIDDEN_1];
    alignas(64) float wm[HIDDEN_1*HIDDEN_1];
    alignas(64) float bm[HIDDEN_1];
    alignas(64) float w2[HIDDEN_1*HIDDEN_2];
    alignas(64) float b2[HIDDEN_2];
    float w3[HIDDEN_2];
    float b3;
};

// What the engine computes with (#55), integers up to the last hidden layer's
// sums, with every scale chosen at load time from the file's weights:
// - Layer 1 is int16: unit j's weights and bias are scaled by scale[j] and
//   rounded, scale[j] as large as it can be while no set of inputs can take the
//   unit's sum out of int16 (|b1| plus its largest |W1| over as many inputs as
//   can be on at once, times the scale, stays under 32768). The clipped ReLU is
//   then clamp(sum, 0, scale[j]).
// - Layers after it are int16 times int16 into int32: W[j][k] / scale[j] *
//   1/w_scale, rounded, with w_scale as small as it can be while no input can
//   take a sum out of int32 and no weight out of int16. Weights are stored by
//   pairs of units, [j/2][k][j%2], as pmaddwd multiplies and adds them.
// - The middle layer's output is clamp(sum*wm_scale + bm, 0, 1) in float, then
//   times MIDDLE_SCALE and rounded to int16, unit by unit in one fixed order.
// - b2, layer 2's clipped ReLU and layer 3 are float, in one fixed order.
// Integer sums are exact, so they are the same whatever order or SIMD width
// they are added in: a first layer can be updated from another position's
// instead of recomputed, and every machine gets the same bits.
constexpr int MIDDLE_SCALE = 8192;

struct Quantized_Net
{
    alignas(64) int16_t w1[N_INPUTS*HIDDEN_1];
    alignas(64) int16_t b1[HIDDEN_1];
    alignas(64) int32_t b1_wide[HIDDEN_1];  // a named net's b1: its sums are int32
    alignas(64) int16_t scale[HIDDEN_1];
    alignas(64) int16_t wm[HIDDEN_1*HIDDEN_1];  // [j/2][k][j%2]
    float wm_scale;
    alignas(64) float bm[HIDDEN_1];
    alignas(64) int16_t w2[HIDDEN_1*HIDDEN_2];  // [j/2][k][j%2]
    float w2_scale;
    alignas(64) float b2[HIDDEN_2];
    float w3[HIDDEN_2];
    float b3;
};

inline Quantized_Net qnet;    // load()'s file, quantized; zero until then
inline int n_words = PIECE_WORDS;  // the input words `qnet` reads: PIECE_WORDS or FEATURE_WORDS
inline bool middle_layer = false;  // `qnet` has the middle 128 layer
inline bool enabled = false;  // UseNNE: the quiet leaf of minimax_tactical() adds the net's correction
inline std::string loaded_path;  // the file in `qnet`, empty if none
inline bool use_avx2 = false;    // set by load() from the CPU; both paths give identical results

// Reads a weights file and quantizes it into `qnet`. The file, little-endian:
// "NNE1", uint32 version, uint32 layer count L, uint32 sizes[L+1], then per
// layer float32 W [in][out] and b [out]. Version 1 is 780 128 16 1; version 2
// (#114) has 780 or 3072 inputs and 3 or 4 layers (128 [128] 16 1). On any
// error `qnet` and loaded_path are left as they were and `error` says why.
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
// eval is, the sum never reaches a TB score (|eval| >= TB_WIN_SCORE) or the
// mate band beyond it.
constexpr int SCORE_LIMIT = TB_WIN_SCORE - 1;
int corrected_eval(const BB* pos, int static_eval);
}

#include "../src/nne.cpp"

#endif // NNE_HPP
