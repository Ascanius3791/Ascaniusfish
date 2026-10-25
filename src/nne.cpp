// OWNERSHIP=Claude
#ifndef NNE_CPP
#define NNE_CPP
#include "../lib/nne.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <immintrin.h>  // layer 2: SSE2 (every x86-64 CPU) and AVX2
#include <functional>
#include <memory>

namespace nne
{
constexpr uint64_t FILE_A = 0x0101010101010101ULL, FILE_H = FILE_A << 7;
static inline uint64_t fill_up(uint64_t b)   { b |= b << 8; b |= b << 16; return b | b << 32; }
static inline uint64_t fill_down(uint64_t b) { b |= b >> 8; b |= b >> 16; return b | b >> 32; }
static inline uint64_t beside(uint64_t b)    { return ((b & ~FILE_A) >> 1) | ((b & ~FILE_H) << 1); }
static inline uint64_t files_of(uint64_t b)  { return fill_up(fill_down(b)); }

void named_features(const uint64_t* w, uint64_t* named)
{
    const uint64_t occupancy = w[0]|w[1]|w[2]|w[3]|w[4]|w[5]|w[6]|w[7]|w[8]|w[9]|w[10]|w[11];
    // Side 0 is the mover, its pawns moving up; side 1 the opponent, moving down.
    uint64_t by_pawn[2], by_minor[2], by_rook[2], attacks[2];
    for(int s=0;s<2;s++)
    {
        const uint64_t* p = w + 6*s;
        by_pawn[s] = s ? ((p[0] & ~FILE_H) >> 7) | ((p[0] & ~FILE_A) >> 9)
                       : ((p[0] & ~FILE_A) << 7) | ((p[0] & ~FILE_H) << 9);
        uint64_t minor = 0, rook = 0, other = 0, bb;
        for(bb = p[2]; bb; bb &= bb-1) minor |= Kn_template[__builtin_ctzll(bb)];
        for(bb = p[3]; bb; bb &= bb-1) minor |= get_bishop_attacks(__builtin_ctzll(bb), occupancy);
        for(bb = p[1]; bb; bb &= bb-1) rook |= get_rook_attacks(__builtin_ctzll(bb), occupancy);
        for(bb = p[4]; bb; bb &= bb-1)
        {
            const int i = __builtin_ctzll(bb);
            other |= get_bishop_attacks(i, occupancy) | get_rook_attacks(i, occupancy);
        }
        if(p[5]) other |= K_template[__builtin_ctzll(p[5])];
        by_minor[s] = minor;
        by_rook[s] = rook;
        attacks[s] = by_pawn[s] | minor | rook | other;
    }

    for(int s=0;s<2;s++)
    {
        const uint64_t* p = w + 6*s;
        const uint64_t* e = w + 6*(1-s);
        const uint64_t P = p[0], R = p[1], N = p[2], B = p[3], Q = p[4], K = p[5], E = e[0];
        const int o = 1-s;
        // Strictly ahead of / behind a square, as this side's pawns move.
        auto ahead  = [s](uint64_t b) { return s ? fill_down(b >> 8) : fill_up(b << 8); };
        auto behind = [s](uint64_t b) { return s ? fill_up(b << 8) : fill_down(b >> 8); };
        auto at = [&](int f) -> uint64_t& { return named[named_word(f, s) - PIECE_WORDS]; };

        const uint64_t passed = P & ~behind(E | beside(E));
        at(PASSED) = passed;
        at(PASSED_FREE) = passed & ~behind(occupancy);
        const uint64_t neighbour_files = files_of(beside(P));
        at(ISOLATED) = P & ~neighbour_files;
        at(DOUBLED) = P & (ahead(P) | behind(P));
        // not isolated, no own pawn beside it on its rank or behind it, its stop square attacked
        const uint64_t level_or_ahead_of_neighbour = s ? fill_down(beside(P)) : fill_up(beside(P));
        const uint64_t stop_attacked = s ? by_pawn[o] << 8 : by_pawn[o] >> 8;
        at(BACKWARD) = P & neighbour_files & ~level_or_ahead_of_neighbour & stop_attacked;
        at(PHALANX) = P & beside(P);
        at(PROTECTED_PAWN) = P & by_pawn[s];
        at(BLOCKED) = P & (s ? occupancy << 8 : occupancy >> 8);
        const uint64_t outposts = (s ? 0x000000FFFFFF0000ULL : 0x0000FFFFFF000000ULL) & by_pawn[s] & ~behind(beside(E));
        at(KNIGHT_OUTPOST) = N & outposts;
        at(BISHOP_OUTPOST) = B & outposts;
        at(PAWN_DEFENDED) = (N|B|R|Q) & by_pawn[s];
        at(HANGING) = (P|N|B|R|Q) & attacks[o] & ~attacks[s];
        at(ATTACKED_BY_LESSER) = ((N|B) & by_pawn[o]) | (R & (by_pawn[o]|by_minor[o]))
                               | (Q & (by_pawn[o]|by_minor[o]|by_rook[o]));
        at(ROOK_OPEN) = R & ~files_of(P|E);
        at(ROOK_HALF_OPEN) = R & ~files_of(P) & files_of(E);
        at(KING_RING_ATTACKED) = K ? K_template[__builtin_ctzll(K)] & attacks[o] : 0;
        at(ATTACKS) = attacks[s];
    }

    uint64_t g = 0;
    const uint64_t LIGHT = 0x55AA55AA55AA55AAULL;
    for(int s=0;s<2;s++)
    {
        const uint64_t* p = w + 6*s;
        const uint64_t* e = w + 6*(1-s);
        if(__builtin_popcountll(p[3]) >= 2) g |= 1ULL << (OWN_BISHOP_PAIR + s);
        if(!(p[1]|p[2]|p[3]|p[4])) g |= 1ULL << (OWN_NO_PIECES + s);
        // basic_eval's unstoppable passer: a free passer the enemy king cannot
        // catch, the enemy without pieces; the mover's king (side 0) moves first
        if(!(e[1]|e[2]|e[3]|e[4]) && e[5])
        {
            const int king = __builtin_ctzll(e[5]);
            for(uint64_t bb = named[named_word(PASSED_FREE, s) - PIECE_WORDS]; bb; bb &= bb-1)
            {
                const int sq = __builtin_ctzll(bb), r = s ? 7 - sq/8 : sq/8;
                const int promotion = s ? sq%8 : 56 + sq%8;
                const int pawn_moves = (7-r) - (r==1);
                const int king_moves = std::max(std::abs(king/8 - promotion/8), std::abs(king%8 - promotion%8)) - s;
                if(king_moves > pawn_moves) { g |= 1ULL << (OWN_UNSTOPPABLE + s); break; }
            }
        }
    }
    if(__builtin_popcountll(w[3]) == 1 && __builtin_popcountll(w[9]) == 1 && !(w[3] & LIGHT) != !(w[9] & LIGHT))
    g |= 1ULL << OPPOSITE_BISHOPS;
    const int phase = std::min(24, __builtin_popcountll(w[2]|w[3]|w[8]|w[9]) + 2*__builtin_popcountll(w[1]|w[7])
                                 + 4*__builtin_popcountll(w[4]|w[10]));
    g |= 1ULL << (PHASE_BUCKET + phase*8/25);
    named[GLOBAL_WORD - PIECE_WORDS] = g;
}

inline uint32_t net_generation = 0;  // counts load()s, so a kept first layer of an older net is never reused

// A layer after the first, `in` units (scaled by in_scale[j]) to `out`, as
// int16 pairs [j/2][k][j%2] with the smallest w_scale no sum overflows with:
// a sum is at most sum_j in_scale[j]*|w| <= sum_j |W|/w_scale + sum_j in_scale[j]/2.
static void quantize_pairs(const float* W, int in, int out, const int16_t* in_scale, int16_t* wq, float& w_scale)
{
    double top_sum = 0, t = 1e30;
    for(int k=0;k<out;k++)
    {
        double sum = 0;
        for(int j=0;j<in;j++)
        sum += std::fabs(W[j*out + k]);
        top_sum = std::max(top_sum, sum);
    }
    double half_scales = 0;
    for(int j=0;j<in;j++)
    {
        half_scales += in_scale[j]*0.5;
        for(int k=0;k<out;k++)
        if(W[j*out + k] != 0)
        t = std::min(t, 32766.5*in_scale[j]/std::fabs(W[j*out + k]));
    }
    if(top_sum > 0)
    t = std::min(t, (2147483647.0 - half_scales)/top_sum);
    t = std::floor(std::min(t, 1e9));
    for(int j=0;j<in;j++)
    for(int k=0;k<out;k++)
    wq[(j/2)*2*out + 2*k + j%2] = int16_t(std::lround(W[j*out + k] / in_scale[j] * t));
    w_scale = float(1/t);
}

// The scales of Quantized_Net, each as large as it can be without an overflow.
// Layer 1 of a piece net: |b1| plus the largest |W1| of the unit over the
// MAX_ACTIVE piece inputs that can be on at once, all rounded, stay inside
// int16. A named net has up to ~490 inputs on, which would leave W1 a few
// steps per weight (2.6 cp off the float net), so its sums are int32 and
// each W1 only has to fit int16, and |b1| at most 2^30: see the static_assert.
// Later layers: see quantize_pairs(). So no position can overflow, whatever
// the net.
constexpr int max_active_named()
{
    int n = MAX_ACTIVE + GLOBAL_CAP;
    for(int f=0;f<NAMED_FEATURES;f++) n += 2*NAMED_CAP[f];
    return n;
}
constexpr double WIDE_BIAS_ROOM = 1 << 30;  // the most |b1|*scale of a named net
static_assert(max_active_named()*32768.0 + WIDE_BIAS_ROOM < 2147483647.0, "a named net's sums fit int32");

static void quantize(const Net& net, int n_inputs, bool middle, Quantized_Net& q)
{
    std::memset(q.w1, 0, sizeof(q.w1));
    std::memset(q.b1, 0, sizeof(q.b1));
    std::memset(q.b1_wide, 0, sizeof(q.b1_wide));
    for(int j=0;j<HIDDEN_1;j++)
    {
        float column[N_INPUTS];
        for(int i=0;i<n_inputs;i++)
        column[i] = std::fabs(net.w1[i*HIDDEN_1 + j]);
        if(n_inputs > N_INPUTS_PIECES)
        {
            const float largest = *std::max_element(column, column + n_inputs);
            double scale = largest > 0 ? std::min(32767.0, std::floor(32767.0 / largest)) : 32767.0;
            if(net.b1[j] != 0) scale = std::min(scale, std::floor(WIDE_BIAS_ROOM / std::fabs(net.b1[j])));
            q.scale[j] = int16_t(std::max(1.0, scale));
            for(int i=0;i<n_inputs;i++)
            q.w1[i*HIDDEN_1 + j] = int16_t(std::lround(net.w1[i*HIDDEN_1 + j] * q.scale[j]));
            q.b1_wide[j] = int32_t(std::lround(net.b1[j] * q.scale[j]));
            continue;
        }
        double bound = std::fabs(net.b1[j]);
        auto add_largest = [&](int from, int to, int k)
        {
            std::partial_sort(column + from, column + from + k, column + to, std::greater<float>());
            for(int i=0;i<k;i++) bound += column[from + i];
        };
        add_largest(0, N_INPUTS_PIECES, MAX_ACTIVE);
        const double room = 32767 - (MAX_ACTIVE + 1)*0.5;  // what rounding each term may add
        const double scale = bound > 0 ? std::min(32767.0, std::floor(room / bound)) : 32767.0;
        q.scale[j] = int16_t(std::max(1.0, scale));
        for(int i=0;i<n_inputs;i++)
        q.w1[i*HIDDEN_1 + j] = int16_t(std::lround(net.w1[i*HIDDEN_1 + j] * q.scale[j]));
        q.b1[j] = int16_t(std::lround(net.b1[j] * q.scale[j]));
    }

    if(middle)
    {
        quantize_pairs(net.wm, HIDDEN_1, HIDDEN_1, q.scale, q.wm, q.wm_scale);
        std::memcpy(q.bm, net.bm, sizeof(q.bm));
        int16_t middle_scale[HIDDEN_1];
        std::fill(middle_scale, middle_scale + HIDDEN_1, int16_t(MIDDLE_SCALE));
        quantize_pairs(net.w2, HIDDEN_1, HIDDEN_2, middle_scale, q.w2, q.w2_scale);
    }
    else
    quantize_pairs(net.w2, HIDDEN_1, HIDDEN_2, q.scale, q.w2, q.w2_scale);
    std::memcpy(q.b2, net.b2, sizeof(q.b2));
    std::memcpy(q.w3, net.w3, sizeof(q.w3));
    q.b3 = net.b3;
}

bool load(const std::string& path, std::string& error)
{
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the weights file is little-endian");
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if(!f)
    {
        error = "cannot open " + path;
        return false;
    }
    uint32_t header[3] = {}, sizes[5] = {};  // magic, version, layer count; layer count + 1 sizes
    bool ok = std::fread(header, 4, 3, f) == 3 && std::memcmp(header, "NNE1", 4) == 0
           && (header[2] == 3 || header[2] == 4) && std::fread(sizes, 4, header[2] + 1, f) == header[2] + 1;
    const bool middle = header[2] == 4;
    const uint32_t n_in = sizes[0];
    if(ok)
    {
        const uint32_t expected[5] = {n_in, HIDDEN_1, middle ? uint32_t(HIDDEN_1) : uint32_t(HIDDEN_2), middle ? uint32_t(HIDDEN_2) : 1u, 1};
        ok = std::memcmp(sizes, expected, 4*(header[2] + 1)) == 0
          && (header[1] == 1 ? n_in == N_INPUTS_PIECES && !middle
                             : header[1] == 2 && (n_in == N_INPUTS_PIECES || n_in == N_INPUTS_NAMED));
    }
    std::unique_ptr<Net> read(new Net);
    auto get = [&](float* p, size_t n) { return std::fread(p, sizeof(float), n, f) == n; };
    ok = ok && get(read->w1, size_t(n_in)*HIDDEN_1) && get(read->b1, HIDDEN_1)
       && (!middle || (get(read->wm, HIDDEN_1*HIDDEN_1) && get(read->bm, HIDDEN_1)))
       && get(read->w2, HIDDEN_1*HIDDEN_2) && get(read->b2, HIDDEN_2)
       && get(read->w3, HIDDEN_2) && get(&read->b3, 1)
       && std::fgetc(f) == EOF;
    std::fclose(f);
    if(!ok)
    {
        error = path + " is not an NNE1 weights file of 780 or 3072 inputs, 128 [128] 16 1";
        return false;
    }
    std::unique_ptr<Quantized_Net> q(new Quantized_Net);
    quantize(*read, int(n_in), middle, *q);
    qnet = *q;
    n_words = n_in == N_INPUTS_PIECES ? PIECE_WORDS : FEATURE_WORDS;
    middle_layer = middle;
    loaded_path = path;
    net_generation++;
    use_avx2 = __builtin_cpu_supports("avx2");
    return true;
}

// Layer 1 is written with GCC vector types 32 bytes wide: in evaluate_avx2()
// each is one AVX2 register, in evaluate_generic() GCC splits it into two SSE
// ones. The later layers need pmaddwd, so they are written twice with intrinsics.
typedef uint16_t U16x16 __attribute__((vector_size(32), __may_alias__));  // unsigned: the sums wrap, see update()
typedef int16_t I16x16 __attribute__((vector_size(32), __may_alias__));
typedef uint32_t U32x8 __attribute__((vector_size(32), __may_alias__));   // a named net's sums, see quantize()
typedef int32_t I32x8 __attribute__((vector_size(32), __may_alias__));
typedef int16_t I16x8 __attribute__((vector_size(16), __may_alias__));
constexpr int ACC_VECTORS = HIDDEN_1/16;
constexpr int WIDE_VECTORS = HIDDEN_1/8;
template<int nw> constexpr bool WIDE = nw > PIECE_WORDS;  // the net reads the named features: int32 sums
template<bool wide> struct Sum_Vector { typedef U16x16 T; };
template<> struct Sum_Vector<true> { typedef U32x8 T; };

// The first layer's sums for one position: the bias plus the W1 row of every
// active input, per unit int16 (a piece net, the first half of `sum`) or int32
// (a named net).
struct Accumulator
{
    alignas(64) uint32_t sum[HIDDEN_1];
    uint64_t words[FEATURE_WORDS];  // the inputs it was summed for (the first n_words)
    uint32_t generation = 0;        // net_generation then; 0 = never filled
};
// The last position of each side to move (index: white to move) per thread. A
// leaf of the search is usually a sibling or a cousin of the previous one with
// the same mover, a handful of inputs away, while a mover's inputs are seen
// from its side and so share little with the other mover's.
thread_local Accumulator kept[2];

static inline __attribute__((always_inline))
void add_row(U16x16* acc, int input)
{
    const U16x16* row = (const U16x16*)(qnet.w1 + input*HIDDEN_1);
    for(int q=0;q<ACC_VECTORS;q++)
    acc[q] += row[q];
}

static inline __attribute__((always_inline))
void subtract_row(U16x16* acc, int input)
{
    const U16x16* row = (const U16x16*)(qnet.w1 + input*HIDDEN_1);
    for(int q=0;q<ACC_VECTORS;q++)
    acc[q] -= row[q];
}

static inline __attribute__((always_inline))
void add_row(U32x8* acc, int input)
{
    const I16x8* row = (const I16x8*)(qnet.w1 + input*HIDDEN_1);
    for(int q=0;q<WIDE_VECTORS;q++)
    acc[q] += (U32x8)__builtin_convertvector(row[q], I32x8);
}

static inline __attribute__((always_inline))
void subtract_row(U32x8* acc, int input)
{
    const I16x8* row = (const I16x8*)(qnet.w1 + input*HIDDEN_1);
    for(int q=0;q<WIDE_VECTORS;q++)
    acc[q] -= (U32x8)__builtin_convertvector(row[q], I32x8);
}

// Brings `a` to the inputs `words`: from the sums it holds when fewer rows
// differ than there are active inputs, else from the bias. Integer sums are
// exact, so both give the same result. They are added modulo 2^16 or 2^32
// (unsigned), so an intermediate sum may leave the range on the way; the final
// one cannot.
template<int nw> static inline __attribute__((always_inline))
void update(Accumulator& a, const uint64_t* words)
{
    bool from_bias = a.generation != net_generation;
    if(!from_bias)
    {
        int active = 0, changed = 0;
        for(int w=0;w<nw;w++)
        {
            active += __builtin_popcountll(words[w]);
            changed += __builtin_popcountll(words[w] ^ a.words[w]);
        }
        from_bias = changed >= active;
    }
    typedef typename Sum_Vector<WIDE<nw>>::T Vector;
    constexpr int VECTORS = WIDE<nw> ? WIDE_VECTORS : ACC_VECTORS;
    const void* bias = WIDE<nw> ? (const void*)qnet.b1_wide : (const void*)qnet.b1;
    Vector acc[VECTORS];
    if(from_bias)
    {
        for(int q=0;q<VECTORS;q++)
        acc[q] = ((const Vector*)bias)[q];
        for(int w=0;w<nw;w++)
        for(uint64_t b = words[w]; b; b &= b-1)
        add_row(acc, 64*w + __builtin_ctzll(b));
    }
    else
    {
        for(int q=0;q<VECTORS;q++)
        acc[q] = ((const Vector*)a.sum)[q];
        for(int w=0;w<nw;w++)
        {
            for(uint64_t b = words[w] & ~a.words[w]; b; b &= b-1)
            add_row(acc, 64*w + __builtin_ctzll(b));
            for(uint64_t b = a.words[w] & ~words[w]; b; b &= b-1)
            subtract_row(acc, 64*w + __builtin_ctzll(b));
        }
    }
    for(int q=0;q<VECTORS;q++)
    ((Vector*)a.sum)[q] = acc[q];
    std::memcpy(a.words, words, sizeof(uint64_t)*nw);
    a.generation = net_generation;
}

// Layer 1's clipped ReLU, clamp(sum, 0, scale[j]), into h1.
template<int nw> static inline __attribute__((always_inline))
void clip(const Accumulator& a, int16_t* h1)
{
    if constexpr(WIDE<nw>)
    {
        const I32x8 zero = {};
        for(int q=0;q<WIDE_VECTORS;q++)
        {
            I32x8 s = ((const I32x8*)a.sum)[q];
            const I32x8 top = __builtin_convertvector(((const I16x8*)qnet.scale)[q], I32x8);
            s = s < zero ? zero : s;
            ((I16x8*)h1)[q] = __builtin_convertvector(s > top ? top : s, I16x8);
        }
        return;
    }
    const I16x16 zero = {};
    for(int q=0;q<ACC_VECTORS;q++)
    {
        I16x16 s = ((const I16x16*)a.sum)[q];
        const I16x16 top = ((const I16x16*)qnet.scale)[q];
        s = s < zero ? zero : s;
        ((I16x16*)h1)[q] = s > top ? top : s;
    }
}

// A later layer's sums, HIDDEN_1 inputs to OUT outputs, two inputs per
// pmaddwd, 16 outputs at a time. All inputs, the ones at 0 included: about
// half are, in no pattern a branch predictor could learn, and listing the
// others costs more than it saves. Integer sums: the two versions agree.
template<int OUT> __attribute__((target("avx2"), always_inline)) static inline
void dense_avx2(const int16_t* h, const int16_t* w, int32_t* sums)
{
    if constexpr(OUT >= 64)
    {
        // The middle layer: 64 outputs at a time in 8 registers, and a pair of
        // inputs both at 0 skipped, which pays for its branch at this width.
        for(int block=0;block<OUT/64;block++)
        {
            __m256i acc[8];
            for(int q=0;q<8;q++) acc[q] = _mm256_setzero_si256();
            for(int p=0;p<HIDDEN_1/2;p++)
            {
                int32_t pair;
                std::memcpy(&pair, h + 2*p, 4);
                if(!pair) continue;
                const __m256i x = _mm256_set1_epi32(pair);
                const __m256i* row = (const __m256i*)(w + 2*p*OUT) + 8*block;
                for(int q=0;q<8;q++)
                acc[q] = _mm256_add_epi32(acc[q], _mm256_madd_epi16(x, _mm256_load_si256(row + q)));
            }
            for(int q=0;q<8;q++)
            _mm256_storeu_si256((__m256i*)(sums + 64*block + 8*q), acc[q]);
        }
        return;
    }
    for(int block=0;block<OUT/16;block++)
    {
        __m256i lo = _mm256_setzero_si256(), hi = _mm256_setzero_si256();
        for(int p=0;p<HIDDEN_1/2;p++)
        {
            int32_t pair;
            std::memcpy(&pair, h + 2*p, 4);
            const __m256i x = _mm256_set1_epi32(pair);
            const __m256i* row = (const __m256i*)(w + 2*p*OUT) + 2*block;
            lo = _mm256_add_epi32(lo, _mm256_madd_epi16(x, _mm256_load_si256(row)));
            hi = _mm256_add_epi32(hi, _mm256_madd_epi16(x, _mm256_load_si256(row + 1)));
        }
        _mm256_storeu_si256((__m256i*)(sums + 16*block), lo);
        _mm256_storeu_si256((__m256i*)(sums + 16*block + 8), hi);
    }
}

template<int OUT> static inline __attribute__((always_inline))
void dense_sse2(const int16_t* h, const int16_t* w, int32_t* sums)
{
    if constexpr(OUT >= 64)
    {
        // as dense_avx2(): 32 outputs at a time in 8 registers, zero pairs skipped
        for(int block=0;block<OUT/32;block++)
        {
            __m128i acc[8];
            for(int q=0;q<8;q++) acc[q] = _mm_setzero_si128();
            for(int p=0;p<HIDDEN_1/2;p++)
            {
                int32_t pair;
                std::memcpy(&pair, h + 2*p, 4);
                if(!pair) continue;
                const __m128i x = _mm_set1_epi32(pair);
                const __m128i* row = (const __m128i*)(w + 2*p*OUT) + 8*block;
                for(int q=0;q<8;q++)
                acc[q] = _mm_add_epi32(acc[q], _mm_madd_epi16(x, _mm_load_si128(row + q)));
            }
            for(int q=0;q<8;q++)
            _mm_storeu_si128((__m128i*)(sums + 32*block + 4*q), acc[q]);
        }
        return;
    }
    for(int block=0;block<OUT/16;block++)
    {
        __m128i s[4] = {_mm_setzero_si128(), _mm_setzero_si128(), _mm_setzero_si128(), _mm_setzero_si128()};
        for(int p=0;p<HIDDEN_1/2;p++)
        {
            int32_t pair;
            std::memcpy(&pair, h + 2*p, 4);
            const __m128i x = _mm_set1_epi32(pair);
            const __m128i* row = (const __m128i*)(w + 2*p*OUT) + 4*block;
            for(int q=0;q<4;q++)
            s[q] = _mm_add_epi32(s[q], _mm_madd_epi16(x, _mm_load_si128(row + q)));
        }
        for(int q=0;q<4;q++)
        _mm_storeu_si128((__m128i*)(sums + 16*block + 4*q), s[q]);
    }
}

// The middle layer's clipped ReLU, in float, scaled to int16, unit by unit.
static inline __attribute__((always_inline))
void middle_clip(const int32_t* sums, int16_t* h)
{
    for(int k=0;k<HIDDEN_1;k++)
    {
        const float x = std::min(std::max(float(sums[k])*qnet.wm_scale + qnet.bm[k], 0.0f), 1.0f);
        h[k] = int16_t(x*MIDDLE_SCALE + 0.5f);  // x >= 0: rounded by truncation, no libm call
    }
}

// b2, layer 2's clipped ReLU and layer 3, float in one fixed order.
static inline __attribute__((always_inline))
float layer_3(const int32_t* sums)
{
    float c = qnet.b3;
    for(int k=0;k<HIDDEN_2;k++)
    {
        const float h2 = float(sums[k])*qnet.w2_scale + qnet.b2[k];
        c += std::min(std::max(h2, 0.0f), 1.0f)*qnet.w3[k];
    }
    return c;
}

// The first layer for `pos`: the thread's kept one brought to it, or one from scratch.
template<int nw> static inline __attribute__((always_inline))
const Accumulator& first_layer(const BB& pos, bool from_scratch, Accumulator& fresh)
{
    uint64_t words[FEATURE_WORDS];
    feature_set(pos, words, nw);
    Accumulator& a = from_scratch ? fresh : kept[pos.white_move ? 1 : 0];
    update<nw>(a, words);
    return a;
}

// One version per shape, the input words and the middle layer known at compile
// time, so the piece net's code is what it was before #114.
template<int nw, bool middle> __attribute__((target("avx2"))) static float evaluate_avx2(const BB& pos, bool from_scratch)
{
    Accumulator fresh;
    alignas(64) int16_t h1[HIDDEN_1];
    alignas(64) int32_t sums[HIDDEN_1];
    clip<nw>(first_layer<nw>(pos, from_scratch, fresh), h1);
    if(middle)
    {
        dense_avx2<HIDDEN_1>(h1, qnet.wm, sums);
        middle_clip(sums, h1);
    }
    dense_avx2<HIDDEN_2>(h1, qnet.w2, sums);
    return layer_3(sums);
}

template<int nw, bool middle> static float evaluate_generic(const BB& pos, bool from_scratch)
{
    Accumulator fresh;
    alignas(64) int16_t h1[HIDDEN_1];
    alignas(64) int32_t sums[HIDDEN_1];
    clip<nw>(first_layer<nw>(pos, from_scratch, fresh), h1);
    if(middle)
    {
        dense_sse2<HIDDEN_1>(h1, qnet.wm, sums);
        middle_clip(sums, h1);
    }
    dense_sse2<HIDDEN_2>(h1, qnet.w2, sums);
    return layer_3(sums);
}

template<int nw, bool middle> static inline float evaluate(const BB& pos, bool from_scratch)
{
    return use_avx2 ? evaluate_avx2<nw, middle>(pos, from_scratch) : evaluate_generic<nw, middle>(pos, from_scratch);
}

static float evaluate(const BB& pos, bool from_scratch)
{
    if(n_words == PIECE_WORDS)
    return middle_layer ? evaluate<PIECE_WORDS, true>(pos, from_scratch) : evaluate<PIECE_WORDS, false>(pos, from_scratch);
    return middle_layer ? evaluate<FEATURE_WORDS, true>(pos, from_scratch) : evaluate<FEATURE_WORDS, false>(pos, from_scratch);
}

float correction(const BB& pos)
{
    return evaluate(pos, false);
}

float correction_from_scratch(const BB& pos)
{
    return evaluate(pos, true);
}

int corrected_eval(const BB* pos, int static_eval)
{
    const double c = correction(*pos);
    const double sum = static_eval + (pos->white_move ? c : -c);
    return (int)std::lround(std::clamp(sum, -double(SCORE_LIMIT), double(SCORE_LIMIT)));
}
}

#endif // NNE_CPP
