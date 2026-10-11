// OWNERSHIP=Claude
// SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104), for the login cookie of
// gui/access.hpp (#109). Written here rather than taken from a library: the
// repo uses no third-party crypto. diagnostics/gui_access_test.cpp checks both
// against the NIST and RFC 4231 test vectors.
#ifndef GUI_SHA256_HPP
#define GUI_SHA256_HPP
#include <cstdint>
#include <cstring>
#include <string>

namespace sha256
{
    constexpr size_t DIGEST_BYTES = 32;
    constexpr size_t BLOCK_BYTES = 64;

    class Hasher
    {
        public:
        Hasher() { reset(); }

        void reset()
        {
            static const uint32_t init[8] = {
                0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
            memcpy(h, init, sizeof h);
            total = 0;
            used = 0;
        }

        void update(const void* data, size_t n)
        {
            const unsigned char* p = (const unsigned char*)data;
            total += n;
            while(n>0)
            {
                size_t take = BLOCK_BYTES-used < n ? BLOCK_BYTES-used : n;
                memcpy(block+used, p, take);
                used += take;
                p += take;
                n -= take;
                if(used==BLOCK_BYTES)
                {
                    compress(block);
                    used = 0;
                }
            }
        }

        void update(const std::string& s) { update(s.data(), s.size()); }

        // The digest as 32 raw bytes. The hasher must be reset() before reuse.
        std::string finish()
        {
            uint64_t bits = total*8;
            unsigned char pad = 0x80;
            update(&pad, 1);
            unsigned char zero = 0;
            while(used!=BLOCK_BYTES-8)
            update(&zero, 1);
            unsigned char len[8];
            for(int i=0;i<8;i++)
            len[i] = (unsigned char)(bits>>(56-8*i));
            update(len, 8);
            std::string out(DIGEST_BYTES, '\0');
            for(int i=0;i<8;i++)
            for(int j=0;j<4;j++)
            out[4*i+j] = (char)(h[i]>>(24-8*j));
            return out;
        }

        private:
        uint32_t h[8];
        uint64_t total;
        unsigned char block[BLOCK_BYTES];
        size_t used;

        static uint32_t rotr(uint32_t x, int n) { return (x>>n) | (x<<(32-n)); }

        void compress(const unsigned char* b)
        {
            static const uint32_t k[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
            uint32_t w[64];
            for(int i=0;i<16;i++)
            w[i] = (uint32_t)b[4*i]<<24 | (uint32_t)b[4*i+1]<<16 | (uint32_t)b[4*i+2]<<8 | (uint32_t)b[4*i+3];
            for(int i=16;i<64;i++)
            {
                uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15]>>3);
                uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2]>>10);
                w[i] = w[i-16] + s0 + w[i-7] + s1;
            }
            uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for(int i=0;i<64;i++)
            {
                uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
                uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
                hh = g; g = f; f = e; e = d + t1;
                d = c; c = bb; bb = a; a = t1 + t2;
            }
            h[0] += a; h[1] += bb; h[2] += c; h[3] += d;
            h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }
    };

    inline std::string digest(const std::string& message)
    {
        Hasher hasher;
        hasher.update(message);
        return hasher.finish();
    }

    // HMAC-SHA256: a key longer than a block is hashed first, a shorter one
    // zero-padded, as RFC 2104 says.
    inline std::string hmac(const std::string& key, const std::string& message)
    {
        std::string k = key.size()>BLOCK_BYTES ? digest(key) : key;
        k.resize(BLOCK_BYTES, '\0');
        std::string ipad(BLOCK_BYTES, '\0'), opad(BLOCK_BYTES, '\0');
        for(size_t i=0;i<BLOCK_BYTES;i++)
        {
            ipad[i] = (char)(k[i] ^ 0x36);
            opad[i] = (char)(k[i] ^ 0x5c);
        }
        Hasher inner;
        inner.update(ipad);
        inner.update(message);
        Hasher outer;
        outer.update(opad);
        outer.update(inner.finish());
        return outer.finish();
    }

    inline std::string hex(const std::string& raw)
    {
        static const char* digits = "0123456789abcdef";
        std::string out(raw.size()*2, '0');
        for(size_t i=0;i<raw.size();i++)
        {
            out[2*i] = digits[(unsigned char)raw[i]>>4];
            out[2*i+1] = digits[(unsigned char)raw[i]&0xF];
        }
        return out;
    }

    // Equal strings, in a time that depends only on their lengths. The callers
    // compare digests, whose length is public.
    inline bool equal(const std::string& a, const std::string& b)
    {
        if(a.size()!=b.size())
        return false;
        unsigned char diff = 0;
        for(size_t i=0;i<a.size();i++)
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
        return diff==0;
    }
}

#endif // GUI_SHA256_HPP
