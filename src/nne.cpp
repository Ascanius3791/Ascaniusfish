// OWNERSHIP=Claude
#ifndef NNE_CPP
#define NNE_CPP
#include "../lib/nne.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace nne
{
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
    net = *read;
    loaded_path = path;
    return true;
}

// Four floats in one SSE register (GCC vector extension; the build has no
// -mavx). Written out by hand: left to itself, GCC unrolls the loop over the
// active inputs by two and fuses the pair into a scalar loop.
typedef float F4 __attribute__((vector_size(16), __may_alias__));
constexpr int F4_PER_CHUNK = 16;  // 64 floats of h1 held in registers at once

float correction(const BB& pos)
{
    static_assert(HIDDEN_1 % (4*F4_PER_CHUNK) == 0 && HIDDEN_2 == 16, "the loops below assume these sizes");
    int active[MAX_ACTIVE];
    const int n = active_features(pos, active);

    alignas(64) float h1[HIDDEN_1];
    for(int chunk=0; chunk<HIDDEN_1; chunk+=4*F4_PER_CHUNK)
    {
        F4 acc[F4_PER_CHUNK];
        for(int q=0;q<F4_PER_CHUNK;q++)
        acc[q] = *(const F4*)(net.b1 + chunk + 4*q);
        for(int k=0;k<n;k++)
        {
            const float* row = net.w1 + active[k]*HIDDEN_1 + chunk;
            for(int q=0;q<F4_PER_CHUNK;q++)
            acc[q] += *(const F4*)(row + 4*q);
        }
        const F4 zero = {0, 0, 0, 0}, one = {1, 1, 1, 1};
        for(int q=0;q<F4_PER_CHUNK;q++)
        {
            F4 a = acc[q] < zero ? zero : acc[q];  // the clipped ReLU, four at a time
            *(F4*)(h1 + chunk + 4*q) = a > one ? one : a;
        }
    }

    // No skipping of the units clipped to 0: about half are, in no pattern a
    // branch predictor can learn, and a mispredicted branch costs more than the
    // four multiply-adds. Two sets of sums, so one chain of additions does not
    // wait on the other.
    F4 even[4], odd[4];
    for(int q=0;q<4;q++)
    {
        even[q] = *(const F4*)(net.b2 + 4*q);
        odd[q] = F4{0, 0, 0, 0};
    }
    for(int i=0;i<HIDDEN_1;i+=2)
    {
        const float* row = net.w2 + i*HIDDEN_2;
        for(int q=0;q<4;q++)
        {
            even[q] += h1[i] * *(const F4*)(row + 4*q);
            odd[q] += h1[i+1] * *(const F4*)(row + HIDDEN_2 + 4*q);
        }
    }

    float c = net.b3;
    for(int q=0;q<4;q++)
    {
        const F4 h2 = even[q] + odd[q];
        for(int j=0;j<4;j++)
        c += std::min(std::max(h2[j], 0.0f), 1.0f)*net.w3[4*q+j];
    }
    return c;
}

int corrected_eval(const BB* pos, int static_eval)
{
    const double c = correction(*pos);
    const double sum = static_eval + (pos->white_move ? c : -c);
    return (int)std::lround(std::clamp(sum, -double(SCORE_LIMIT), double(SCORE_LIMIT)));
}
}

#endif // NNE_CPP
