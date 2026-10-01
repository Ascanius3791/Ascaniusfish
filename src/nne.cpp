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
inline uint32_t net_generation = 0;  // counts load()s, so a kept first layer of an older net is never reused

// The scales of Quantized_Net, each as large as it can be without an overflow.
// Layer 1: |b1| plus the MAX_ACTIVE largest |W1| of the unit, all rounded, stay
// inside int16; no position has more active inputs than that. Layer 2: h1 at
// its top for every unit, against the largest sum of |W2| over the units, stays
// inside int32. So no position can overflow, whatever the net.
static void quantize(const Net& net, Quantized_Net& q)
{
    for(int j=0;j<HIDDEN_1;j++)
    {
        float column[N_INPUTS];
        for(int i=0;i<N_INPUTS;i++)
        column[i] = std::fabs(net.w1[i*HIDDEN_1 + j]);
        std::partial_sort(column, column + MAX_ACTIVE, column + N_INPUTS, std::greater<float>());
        double bound = std::fabs(net.b1[j]);
        for(int k=0;k<MAX_ACTIVE;k++)
        bound += column[k];
        const double room = 32767 - (MAX_ACTIVE + 1)*0.5;  // what rounding each term may add
        const double scale = bound > 0 ? std::min(32767.0, std::floor(room / bound)) : 32767.0;
        q.scale[j] = int16_t(std::max(1.0, scale));
        for(int i=0;i<N_INPUTS;i++)
        q.w1[i*HIDDEN_1 + j] = int16_t(std::lround(net.w1[i*HIDDEN_1 + j] * q.scale[j]));
        q.b1[j] = int16_t(std::lround(net.b1[j] * q.scale[j]));
    }

    // w2 = W2 / scale[j] * T, rounded. A sum is at most sum_j scale[j]*|w2|
    // <= T*sum_j |W2| + sum_j scale[j]/2.
    double top_sum = 0, t = 1e30;
    for(int k=0;k<HIDDEN_2;k++)
    {
        double sum = 0;
        for(int j=0;j<HIDDEN_1;j++)
        sum += std::fabs(net.w2[j*HIDDEN_2 + k]);
        top_sum = std::max(top_sum, sum);
    }
    double half_scales = 0;
    for(int j=0;j<HIDDEN_1;j++)
    {
        half_scales += q.scale[j]*0.5;
        for(int k=0;k<HIDDEN_2;k++)
        if(net.w2[j*HIDDEN_2 + k] != 0)
        t = std::min(t, 32766.5*q.scale[j]/std::fabs(net.w2[j*HIDDEN_2 + k]));
    }
    if(top_sum > 0)
    t = std::min(t, (2147483647.0 - half_scales)/top_sum);
    t = std::floor(std::min(t, 1e9));
    for(int j=0;j<HIDDEN_1;j++)
    for(int k=0;k<HIDDEN_2;k++)
    q.w2[(j/2)*2*HIDDEN_2 + 2*k + j%2] = int16_t(std::lround(net.w2[j*HIDDEN_2 + k] / q.scale[j] * t));
    q.w2_scale = float(1/t);
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
    uint32_t header[7];  // magic, version, layer count, 4 sizes
    std::unique_ptr<Net> read(new Net);
    const bool complete =
        std::fread(header, 4, 7, f) == 7
     && std::fread(read->w1, sizeof(float), N_INPUTS*HIDDEN_1, f) == size_t(N_INPUTS*HIDDEN_1)
     && std::fread(read->b1, sizeof(float), HIDDEN_1, f) == size_t(HIDDEN_1)
     && std::fread(read->w2, sizeof(float), HIDDEN_1*HIDDEN_2, f) == size_t(HIDDEN_1*HIDDEN_2)
     && std::fread(read->b2, sizeof(float), HIDDEN_2, f) == size_t(HIDDEN_2)
     && std::fread(read->w3, sizeof(float), HIDDEN_2, f) == size_t(HIDDEN_2)
     && std::fread(&read->b3, sizeof(float), 1, f) == 1;
    const bool trailing = std::fgetc(f) != EOF;
    std::fclose(f);
    const uint32_t expected[7] = {0, 1, 3, N_INPUTS, HIDDEN_1, HIDDEN_2, 1};
    if(!complete || std::memcmp(header, "NNE1", 4) != 0
    || std::memcmp(header+1, expected+1, sizeof(uint32_t)*6) != 0 || trailing)
    {
        error = path + " is not a 780-128-16-1 NNE1 version 1 weights file";
        return false;
    }
    std::unique_ptr<Quantized_Net> q(new Quantized_Net);
    quantize(*read, *q);
    qnet = *q;
    loaded_path = path;
    net_generation++;
    use_avx2 = __builtin_cpu_supports("avx2");
    return true;
}

// Layer 1 is written with GCC vector types 32 bytes wide: in evaluate_avx2()
// each is one AVX2 register, in evaluate_generic() GCC splits it into two SSE
// ones. Layer 2 needs pmaddwd, so it is written twice with intrinsics.
typedef uint16_t U16x16 __attribute__((vector_size(32), __may_alias__));  // unsigned: the sums wrap, see update()
typedef int16_t I16x16 __attribute__((vector_size(32), __may_alias__));
constexpr int ACC_VECTORS = HIDDEN_1/16;

// The first layer's sums for one position: the bias plus the W1 row of every
// active input, int16 per unit.
struct Accumulator
{
    alignas(64) uint16_t sum[HIDDEN_1];
    uint64_t words[FEATURE_WORDS];  // the inputs it was summed for
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

// Brings `a` to the inputs `words`: from the sums it holds when fewer rows
// differ than there are active inputs, else from the bias. Integer sums are
// exact, so both give the same result. They are added modulo 2^16 (unsigned),
// so an intermediate sum may leave int16 on the way; the final one cannot.
static inline __attribute__((always_inline))
void update(Accumulator& a, const uint64_t* words)
{
    bool from_bias = a.generation != net_generation;
    if(!from_bias)
    {
        int active = 0, changed = 0;
        for(int w=0;w<FEATURE_WORDS;w++)
        {
            active += __builtin_popcountll(words[w]);
            changed += __builtin_popcountll(words[w] ^ a.words[w]);
        }
        from_bias = changed >= active;
    }
    U16x16 acc[ACC_VECTORS];
    if(from_bias)
    {
        for(int q=0;q<ACC_VECTORS;q++)
        acc[q] = ((const U16x16*)qnet.b1)[q];
        for(int w=0;w<FEATURE_WORDS;w++)
        for(uint64_t b = words[w]; b; b &= b-1)
        add_row(acc, 64*w + __builtin_ctzll(b));
    }
    else
    {
        for(int q=0;q<ACC_VECTORS;q++)
        acc[q] = ((const U16x16*)a.sum)[q];
        for(int w=0;w<FEATURE_WORDS;w++)
        {
            for(uint64_t b = words[w] & ~a.words[w]; b; b &= b-1)
            add_row(acc, 64*w + __builtin_ctzll(b));
            for(uint64_t b = a.words[w] & ~words[w]; b; b &= b-1)
            subtract_row(acc, 64*w + __builtin_ctzll(b));
        }
    }
    for(int q=0;q<ACC_VECTORS;q++)
    ((U16x16*)a.sum)[q] = acc[q];
    std::memcpy(a.words, words, sizeof(a.words));
    a.generation = net_generation;
}

// Layer 1's clipped ReLU, clamp(sum, 0, scale[j]), into h1.
static inline __attribute__((always_inline))
void clip(const Accumulator& a, int16_t* h1)
{
    const I16x16 zero = {};
    for(int q=0;q<ACC_VECTORS;q++)
    {
        I16x16 s = ((const I16x16*)a.sum)[q];
        const I16x16 top = ((const I16x16*)qnet.scale)[q];
        s = s < zero ? zero : s;
        ((I16x16*)h1)[q] = s > top ? top : s;
    }
}

// Layer 2's sums, two units per pmaddwd. All units, the ones at 0 included:
// about half are, in no pattern a branch predictor could learn, and listing
// the others costs more than it saves. Integer sums: the two versions agree.
__attribute__((target("avx2"), always_inline)) static inline
void layer_2_avx2(const int16_t* h1, int32_t* sums)
{
    static_assert(HIDDEN_2 == 16, "two registers of sums");
    __m256i lo = _mm256_setzero_si256(), hi = _mm256_setzero_si256();
    for(int p=0;p<HIDDEN_1/2;p++)
    {
        int32_t pair;
        std::memcpy(&pair, h1 + 2*p, 4);
        const __m256i h = _mm256_set1_epi32(pair);
        const __m256i* row = (const __m256i*)(qnet.w2 + 2*p*HIDDEN_2);
        lo = _mm256_add_epi32(lo, _mm256_madd_epi16(h, _mm256_load_si256(row)));
        hi = _mm256_add_epi32(hi, _mm256_madd_epi16(h, _mm256_load_si256(row + 1)));
    }
    _mm256_storeu_si256((__m256i*)sums, lo);
    _mm256_storeu_si256((__m256i*)(sums + 8), hi);
}

static inline __attribute__((always_inline))
void layer_2_sse2(const int16_t* h1, int32_t* sums)
{
    __m128i s[4] = {_mm_setzero_si128(), _mm_setzero_si128(), _mm_setzero_si128(), _mm_setzero_si128()};
    for(int p=0;p<HIDDEN_1/2;p++)
    {
        int32_t pair;
        std::memcpy(&pair, h1 + 2*p, 4);
        const __m128i h = _mm_set1_epi32(pair);
        const __m128i* row = (const __m128i*)(qnet.w2 + 2*p*HIDDEN_2);
        for(int q=0;q<4;q++)
        s[q] = _mm_add_epi32(s[q], _mm_madd_epi16(h, _mm_load_si128(row + q)));
    }
    for(int q=0;q<4;q++)
    _mm_storeu_si128((__m128i*)(sums + 4*q), s[q]);
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
static inline __attribute__((always_inline))
const Accumulator& first_layer(const BB& pos, bool from_scratch, Accumulator& fresh)
{
    uint64_t words[FEATURE_WORDS];
    feature_set(pos, words);
    Accumulator& a = from_scratch ? fresh : kept[pos.white_move ? 1 : 0];
    update(a, words);
    return a;
}

__attribute__((target("avx2"))) static float evaluate_avx2(const BB& pos, bool from_scratch)
{
    Accumulator fresh;
    alignas(64) int16_t h1[HIDDEN_1];
    alignas(64) int32_t sums[HIDDEN_2];
    clip(first_layer(pos, from_scratch, fresh), h1);
    layer_2_avx2(h1, sums);
    return layer_3(sums);
}

static float evaluate_generic(const BB& pos, bool from_scratch)
{
    Accumulator fresh;
    alignas(64) int16_t h1[HIDDEN_1];
    alignas(64) int32_t sums[HIDDEN_2];
    clip(first_layer(pos, from_scratch, fresh), h1);
    layer_2_sse2(h1, sums);
    return layer_3(sums);
}

float correction(const BB& pos)
{
    return use_avx2 ? evaluate_avx2(pos, false) : evaluate_generic(pos, false);
}

float correction_from_scratch(const BB& pos)
{
    return use_avx2 ? evaluate_avx2(pos, true) : evaluate_generic(pos, true);
}

int corrected_eval(const BB* pos, int static_eval)
{
    const double c = correction(*pos);
    const double sum = static_eval + (pos->white_move ? c : -c);
    return (int)std::lround(std::clamp(sum, -double(SCORE_LIMIT), double(SCORE_LIMIT)));
}
}

#endif // NNE_CPP
