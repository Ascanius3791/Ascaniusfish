// OWNERSHIP=Claude
#ifndef SYZYGY_CPP
#define SYZYGY_CPP
#include "../lib/syzygy.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace syzygy
{
namespace detail
{

// ---------------------------------------------------------------------------
// Constants of the format
// ---------------------------------------------------------------------------

constexpr uint32_t WDL_MAGIC = 0x5d23e871;  // first 4 bytes of a .rtbw, little-endian
constexpr uint32_t DTZ_MAGIC = 0xa50c66d7;  // first 4 bytes of a .rtbz

// Byte 4 of the file.
constexpr uint8_t FILE_SPLIT = 1;      // WDL: both sides to move stored
constexpr uint8_t FILE_HAS_PAWNS = 2;

// First byte of a compressed stream (the "flags" of its pairs data).
constexpr uint8_t PAIRS_STM = 1;           // DTZ: the side to move this stream is for
constexpr uint8_t PAIRS_MAPPED = 2;        // DTZ: values go through the map
constexpr uint8_t PAIRS_WIN_PLIES = 4;     // DTZ: wins stored in plies, not moves
constexpr uint8_t PAIRS_LOSS_PLIES = 8;    // DTZ: losses stored in plies
constexpr uint8_t PAIRS_WIDE = 16;         // DTZ: the map holds 16-bit values
constexpr uint8_t PAIRS_SINGLE = 128;      // one value for every position

// A piece in the file is a nibble: type 1..6 = P N B R Q K, plus 8 for black.
// Board[] index of each type (Board[] order is P R N B Q K).
constexpr int CODE_TO_BOARD[8] = {-1, 0, 2, 3, 1, 4, 5, -1};

constexpr int MAX_SYMBOL_LENGTHS = 32;  // max_len - min_len + 1 of a Huffman code

// ---------------------------------------------------------------------------
// Index tables, all derived from their definitions at startup
// ---------------------------------------------------------------------------

uint64_t binom[MAX_PIECES+1][65];   // binom[k][n] = C(n, k)
int tri_idx[64];                    // a1-d1-d4 triangle: b1 c1 d1 c2 d2 d3 = 0..5, a1 b2 c3 d4 = 6..9
int below_idx[64];                  // squares below a1-h8 in square order: b1..h1 = 0..6, c2.. = 7.., h7 = 27
int kk_idx[10][64];                 // two kings, the first in the triangle: 0..461, -1 = not encoded
int pawn_twist[64];                 // pawn order for the leading pawns (see init_index_tables)
int pawn_flap[64];                  // file group * 6 + rank - 1, files mirrored to a-d
uint64_t lead_pawn_idx[MAX_PIECES][24];     // [lead pawns - 1][flap]: first index with that leading pawn
uint64_t lead_pawn_count[MAX_PIECES][4];    // [lead pawns - 1][file a-d]: positions of the leading pawns

inline int rank_of(int sq) { return sq >> 3; }
inline int file_of(int sq) { return sq & 7; }
// > 0 above the a1-h8 diagonal, < 0 below it, 0 on it
inline int off_diag(int sq) { return rank_of(sq) - file_of(sq); }
inline int flip_diag(int sq) { return ((sq >> 3) | (sq << 3)) & 63; }
inline bool kings_touch(int a, int b)
{
    int df = file_of(a) - file_of(b), dr = rank_of(a) - rank_of(b);
    return df >= -1 && df <= 1 && dr >= -1 && dr <= 1;
}

bool index_tables_ready = false;

void init_index_tables()
{
    if(index_tables_ready)
    return;
    for(int n=0; n<=64; n++)
    for(int k=0; k<=MAX_PIECES; k++)
    binom[k][n] = k==0 ? 1 : n==0 ? 0 : binom[k-1][n-1] + binom[k][n-1];

    // The triangle: off-diagonal squares first, in square order, then the diagonal.
    int code = 0;
    for(int sq=0; sq<64; sq++)
    tri_idx[sq] = -1;
    for(int sq=0; sq<64; sq++)
    if(file_of(sq) <= 3 && rank_of(sq) <= 3 && off_diag(sq) < 0)
    tri_idx[sq] = code++;
    for(int sq=0; sq<64; sq++)
    if(file_of(sq) <= 3 && off_diag(sq) == 0)
    tri_idx[sq] = code++;

    code = 0;
    for(int sq=0; sq<64; sq++)
    below_idx[sq] = off_diag(sq) < 0 ? code++ : -1;

    // Two kings. The first in the triangle, the second anywhere not touching it;
    // if the first is on the diagonal the second is not above it (the diagonal
    // flip took it below). Placements with both on the diagonal come last.
    code = 0;
    for(int t=0; t<10; t++)
    for(int sq=0; sq<64; sq++)
    kk_idx[t][sq] = -1;
    int diag_first[64*10][2], n_diag = 0;
    for(int t=0; t<10; t++)
    {
        int k1 = 0;
        while(tri_idx[k1] != t) k1++;
        for(int k2=0; k2<64; k2++)
        {
            if(kings_touch(k1, k2))
            continue;
            if(off_diag(k1) == 0 && off_diag(k2) > 0)
            continue;
            if(off_diag(k1) == 0 && off_diag(k2) == 0)
            {
                diag_first[n_diag][0] = t;
                diag_first[n_diag][1] = k2;
                n_diag++;
                continue;
            }
            kk_idx[t][k2] = code++;
        }
    }
    for(int i=0; i<n_diag; i++)
    kk_idx[diag_first[i][0]][diag_first[i][1]] = code++;

    // Pawns (ranks 2-7 only). pawn_twist orders the squares by file group,
    // centre files (d, e) first and edge files (a, h) last; inside a group from
    // the 7th rank down to the 2nd, the right-hand file (e f g h) before the
    // left-hand one. The leading pawn is the one nearest the edge, so every
    // other pawn of its colour has a lower pawn_twist than it has.
    for(int sq=0; sq<64; sq++)
    {
        pawn_twist[sq] = pawn_flap[sq] = -1;
        int r = rank_of(sq), f = file_of(sq);
        if(r < 1 || r > 6)
        continue;
        int group = f <= 3 ? 3 - f : f - 4;
        pawn_twist[sq] = 12*group + 2*(6-r) + (f <= 3);
        pawn_flap[sq] = 6*(f <= 3 ? f : 7 - f) + r - 1;
    }
    for(int lead=0; lead<MAX_PIECES; lead++)   // lead = leading pawns - 1
    for(int file=0; file<4; file++)
    {
        uint64_t s = 0;
        for(int r=1; r<=6; r++)
        {
            int sq = 8*r + file;
            lead_pawn_idx[lead][pawn_flap[sq]] = s;
            s += binom[lead][pawn_twist[sq]];
        }
        lead_pawn_count[lead][file] = s;
    }
    index_tables_ready = true;
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

inline uint16_t read_u16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
inline uint32_t read_u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
inline uint32_t read_be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]); }
inline uint64_t read_be64(const uint8_t* p) { return uint64_t(read_be32(p)) << 32 | read_be32(p+4); }

// One compressed stream of values: a table's values for one side to move (and,
// with pawns, one file of the leading pawn). Values are grouped into blocks of
// Huffman codes; each code is a symbol, and a symbol expands, through a binary
// tree of pairs (sympat), into a run of symlen+1 values.
struct Pairs
{
    uint8_t flags = 0;
    uint8_t single_value = 0;         // PAIRS_SINGLE: every position's value
    int block_size = 0;               // log2 of a block's bytes
    int idx_bits = 0;                 // log2 of the positions per sparse index entry
    int min_len = 0;                  // shortest code, in bits
    uint32_t real_blocks = 0;         // blocks holding data
    uint32_t blocks = 0;              // entries of block_len (real_blocks plus padding)
    uint64_t sparse_entries = 0;
    const uint8_t* sparse = nullptr;      // per 2^idx_bits positions: u32 block, u16 offset
    const uint8_t* block_len = nullptr;   // u16 per block: values in it minus 1
    const uint8_t* data = nullptr;        // the blocks
    const uint8_t* lowest_sym = nullptr;  // u16 per code length: its first symbol
    const uint8_t* sympat = nullptr;      // 3 bytes per symbol: two 12-bit halves
    const uint8_t* symlen = nullptr;      // per symbol: values it expands to minus 1
    uint64_t base[MAX_SYMBOL_LENGTHS];    // lowest code of each length, left-aligned in 64 bits
    uint16_t map_idx[4];                  // DTZ with PAIRS_MAPPED: where each WDL's map starts
};

// One side to move (and file of the leading pawn) of a table: the order the
// pieces are encoded in, how they are grouped, and the multiplier of each group.
struct Encoding
{
    uint8_t pieces[MAX_PIECES];   // file piece codes, in encoding order
    int group[MAX_PIECES];        // group size at its first piece, 0 inside a group
    uint64_t factor[MAX_PIECES];  // index multiplier of the group starting there
    uint64_t size = 0;            // positions encoded
    Pairs pairs;
};

// A parsed table: pointers into the file's mapping.
struct Table
{
    bool dtz = false;
    bool has_pawns = false;
    bool symmetric = false;       // same material for both colours
    int num = 0;                  // pieces
    int lead = 0;                 // pawnless: pieces encoded together first (3 or 2); pawns: leading pawns
    int other_pawns = 0;          // pawns of the other colour
    int sides = 1;                // WDL split: 2
    int files = 1;                // with pawns: 4 (file of the leading pawn, a-d)
    Encoding enc[4][2];           // [file][side]
    const uint8_t* map = nullptr; // DTZ value maps
    std::vector<uint8_t> symlen;  // every stream's symlen
};

// A mapped file. init() maps it and nothing more; its header is parsed by the
// first probe that needs it (get_table()), so a table no game reaches costs no
// memory beyond its mapping, and starting up reads nothing from disk. The
// headers of all 290 files of the 3-4-5 set hold ~7 MB of Huffman trees.
struct Table_File
{
    char name[16] = {};
    const uint8_t* mapping = nullptr;
    size_t mapping_size = 0;
    std::atomic<Table*> table{nullptr};
    std::atomic<bool> broken{false};
};

constexpr int MAX_TABLES = 256;
Table_File table_files[2][MAX_TABLES];   // [dtz][]
int table_count[2] = {0, 0};
int largest = 0;
std::string table_dir;
std::mutex parse_mutex;

// Material key: 4 bits per Board[] index.
constexpr int KEY_SLOTS = 1024;
struct Key_Slot { uint64_t key; int16_t table[2]; bool swapped; };
Key_Slot key_slots[KEY_SLOTS];

inline uint64_t material_key(const uint64_t board[12])
{
    uint64_t key = 0;
    for(int i=0; i<12; i++)
    key |= uint64_t(__builtin_popcountll(board[i])) << (4*i);
    return key;
}

Key_Slot* find_slot(uint64_t key)
{
    uint64_t h = (key * 0x9E3779B97F4A7C15ULL) >> 54;  // 10 bits
    for(int i=0; i<KEY_SLOTS; i++)
    {
        Key_Slot& s = key_slots[(h + i) & (KEY_SLOTS-1)];
        if(s.key == key || s.key == 0)
        return &s;
    }
    return nullptr;
}

// "KQRvKP" -> piece counts per Board[] index, white = the part before the v.
bool parse_name(const char* name, int counts[12])
{
    for(int i=0; i<12; i++) counts[i] = 0;
    int colour = 0;
    bool seen_v = false;
    for(const char* c = name; *c; c++)
    {
        int type;
        switch(*c)
        {
            case 'P': type = 0; break;
            case 'R': type = 1; break;
            case 'N': type = 2; break;
            case 'B': type = 3; break;
            case 'Q': type = 4; break;
            case 'K': type = 5; break;
            case 'v': if(seen_v) return false; seen_v = true; colour = 1; continue;
            default: return false;
        }
        counts[type + 6*colour]++;
    }
    return seen_v && counts[5]==1 && counts[11]==1;
}

// symlen[sym]: a symbol whose right half is 0xfff is a single value, any other
// is the pair (left, right) of two shorter runs. state: 0 new, 1 being
// computed, 2 done. False on a malformed tree (a cycle, a run too long).
bool calc_symlen(const uint8_t* sympat, int syms, uint8_t* symlen, uint8_t* state, int sym, int depth)
{
    if(state[sym] == 2)
    return true;
    if(state[sym] == 1 || depth > 256)
    return false;
    state[sym] = 1;
    const uint8_t* w = sympat + 3*sym;
    const int left = (w[1] & 0xf) << 8 | w[0];
    const int right = w[2] << 4 | w[1] >> 4;
    if(right == 0xfff)
    symlen[sym] = 0;
    else
    {
        if(left >= syms || right >= syms
           || !calc_symlen(sympat, syms, symlen, state, left, depth + 1)
           || !calc_symlen(sympat, syms, symlen, state, right, depth + 1))
        return false;
        const int len = symlen[left] + symlen[right] + 1;
        if(len > 255)
        return false;
        symlen[sym] = uint8_t(len);
    }
    state[sym] = 2;
    return true;
}

// Parses one compressed stream's header at d[p]; returns the offset after it.
size_t setup_pairs(const uint8_t* d, size_t p, uint64_t positions, Pairs& pr, std::vector<uint8_t>& symlen_pool,
                   uint64_t& sparse_bytes, uint64_t& len_bytes, uint64_t& data_bytes, bool& ok)
{
    pr.flags = d[p];
    sparse_bytes = len_bytes = data_bytes = 0;
    if(pr.flags & PAIRS_SINGLE)
    {
        pr.single_value = d[p+1];
        return p + 2;
    }
    pr.block_size = d[p+1];
    pr.idx_bits = d[p+2];
    pr.real_blocks = read_u32(d + p + 4);
    pr.blocks = pr.real_blocks + d[p+3];
    int max_len = d[p+8];
    pr.min_len = d[p+9];
    int lengths = max_len - pr.min_len + 1;
    if(lengths < 1 || lengths > MAX_SYMBOL_LENGTHS || pr.idx_bits < 1 || pr.idx_bits > 40)
    {
        ok = false;
        return p;
    }
    pr.lowest_sym = d + p + 10;
    int syms = read_u16(d + p + 10 + 2*lengths);
    pr.sympat = d + p + 12 + 2*lengths;

    // Canonical Huffman code in which longer codes have lower values. base[i]
    // is the lowest code of length min_len+i, left-aligned in 64 bits: a code
    // read from the stream has that length if it is >= base[i] and < base[i-1].
    pr.base[lengths-1] = 0;
    for(int i=lengths-2; i>=0; i--)
    pr.base[i] = (pr.base[i+1] + read_u16(pr.lowest_sym + 2*i) - read_u16(pr.lowest_sym + 2*(i+1))) / 2;
    for(int i=0; i<lengths; i++)
    pr.base[i] <<= 64 - (pr.min_len + i);

    size_t at = symlen_pool.size();
    symlen_pool.resize(at + syms);
    std::vector<uint8_t> state(syms, 0);
    for(int sym=0; sym<syms; sym++)
    if(!calc_symlen(pr.sympat, syms, symlen_pool.data() + at, state.data(), sym, 0))
    {
        ok = false;
        return p;
    }
    pr.symlen = (const uint8_t*)at;  // an offset until parse_table() is done with the pool

    pr.sparse_entries = (positions + (1ULL << pr.idx_bits) - 1) >> pr.idx_bits;
    sparse_bytes = 6 * pr.sparse_entries;
    len_bytes = 2ULL * pr.blocks;
    data_bytes = uint64_t(pr.real_blocks) << pr.block_size;
    return p + 12 + 2*lengths + 3*syms + (syms & 1);
}

// Groups and factors of one side's encoding (order/order2: where the leading
// group and the other colour's pawns come in the multiplication order).
void set_encoding(const Table& t, Encoding& e, int order, int order2, int file)
{
    for(int i=0; i<MAX_PIECES; i++)
    e.group[i] = 0;
    e.group[0] = t.lead;
    int i = t.lead;
    if(t.other_pawns)
    {
        e.group[i] = t.other_pawns;
        i += t.other_pawns;
    }
    while(i < t.num)
    {
        int j = i;
        while(j < t.num && e.pieces[j] == e.pieces[i]) j++;
        e.group[i] = j - i;
        i = j;
    }

    // Multiply the groups together in the file's order. The leading group
    // counts its own placements; every other group chooses its squares among
    // the ones the groups before it (in piece order) left free.
    i = t.lead + (t.has_pawns && order2 < 15 ? t.other_pawns : 0);
    int free = 64 - i;
    uint64_t f = 1;
    for(int k=0; i < t.num || k == order || k == order2; k++)
    {
        if(k == order)
        {
            e.factor[0] = f;
            if(t.has_pawns)
            f *= lead_pawn_count[t.lead-1][file];
            else
            f *= t.lead == 3 ? 31332 : 462;
        }
        else if(k == order2)
        {
            e.factor[t.lead] = f;
            f *= binom[t.other_pawns][48 - t.lead];
        }
        else
        {
            e.factor[i] = f;
            f *= binom[e.group[i]][free];
            free -= e.group[i];
            i += e.group[i];
        }
    }
    e.size = f;
}

void report(const char* path, const char* what)
{
    std::fprintf(stderr, "syzygy: %s: %s, skipped\n", path, what);
}

bool parse_table(Table& t, const uint8_t* d, size_t size, const int counts[12], const char* path)
{
    if(size < 16 || read_u32(d) != (t.dtz ? DTZ_MAGIC : WDL_MAGIC))
    {
        report(path, "not a Syzygy file");
        return false;
    }
    t.num = 0;
    for(int i=0; i<12; i++)
    t.num += counts[i];
    t.has_pawns = counts[0] + counts[6] > 0;
    t.symmetric = true;
    for(int i=0; i<6; i++)
    t.symmetric &= counts[i] == counts[i+6];
    if(bool(d[4] & FILE_HAS_PAWNS) != t.has_pawns)
    {
        report(path, "pawn flag does not match the name");
        return false;
    }
    t.files = t.has_pawns ? 4 : 1;
    t.sides = !t.dtz && (d[4] & FILE_SPLIT) ? 2 : 1;
    if(t.has_pawns)
    {
        // The leading colour has the fewer pawns (white if equal or black has none).
        bool white_leads = counts[6] == 0 || (counts[0] > 0 && counts[6] >= counts[0]);
        t.lead = white_leads ? counts[0] : counts[6];
        t.other_pawns = white_leads ? counts[6] : counts[0];
    }
    else
    {
        // Three unique pieces (the kings and one more) are encoded together;
        // otherwise the two kings are.
        int unique = 0;
        for(int i=0; i<12; i++)
        unique += counts[i] == 1;
        t.lead = unique >= 3 ? 3 : 2;
        t.other_pawns = 0;
    }

    size_t p = 5;
    int order[4][2], order2[4][2];
    for(int f=0; f<t.files; f++)
    {
        bool pp = t.other_pawns > 0;
        order[f][0] = d[p] & 0xf;
        order[f][1] = d[p] >> 4;
        order2[f][0] = pp ? d[p+1] & 0xf : 0xf;
        order2[f][1] = pp ? d[p+1] >> 4 : 0xf;
        p += 1 + pp;
        for(int k=0; k<t.num; k++)
        {
            t.enc[f][0].pieces[k] = d[p+k] & 0xf;
            t.enc[f][1].pieces[k] = d[p+k] >> 4;
        }
        p += t.num;
    }
    p += p & 1;

    // Check the pieces are the table's material, and each group is one piece kind.
    for(int f=0; f<t.files; f++)
    for(int s=0; s<t.sides; s++)
    {
        const Encoding& e = t.enc[f][s];
        int seen[12] = {};
        for(int k=0; k<t.num; k++)
        {
            int code = e.pieces[k], type = code & 7;
            if(type < 1 || type > 6 || (code & ~0xf))
            {
                report(path, "bad piece code");
                return false;
            }
            seen[CODE_TO_BOARD[type] + 6*(code >> 3)]++;
        }
        for(int i=0; i<12; i++)
        if(seen[i] != counts[i])
        {
            report(path, "pieces do not match the name");
            return false;
        }
        if(t.has_pawns)
        for(int k=0; k<t.lead+t.other_pawns; k++)
        if((e.pieces[k] & 7) != 1 || (k < t.lead ? e.pieces[k] != e.pieces[0] : e.pieces[k] == e.pieces[0]))
        {
            report(path, "unexpected pawn order");
            return false;
        }
        if(t.has_pawns && e.pieces[0] != t.enc[0][0].pieces[0])
        {
            report(path, "leading pawns change colour");
            return false;
        }
        if(!t.has_pawns && t.lead == 2 && ((e.pieces[0] & 7) != 6 || (e.pieces[1] & 7) != 6))
        {
            report(path, "kings do not lead");
            return false;
        }
    }

    for(int f=0; f<t.files; f++)
    for(int s=0; s<t.sides; s++)
    set_encoding(t, t.enc[f][s], order[f][s], order2[f][s], f);

    uint64_t sparse_bytes[4][2], len_bytes[4][2], data_bytes[4][2];
    bool ok = true;
    for(int f=0; f<t.files; f++)
    for(int s=0; s<t.sides; s++)
    {
        p = setup_pairs(d, p, t.enc[f][s].size, t.enc[f][s].pairs, t.symlen, sparse_bytes[f][s], len_bytes[f][s], data_bytes[f][s], ok);
        if(!ok || p > size)
        {
            report(path, "bad compression header");
            return false;
        }
    }

    if(t.dtz)
    {
        t.map = d + p;
        for(int f=0; f<t.files; f++)
        {
            Pairs& pr = t.enc[f][0].pairs;
            if(!(pr.flags & PAIRS_MAPPED))
            continue;
            if(pr.flags & PAIRS_WIDE)
            {
                p += p & 1;
                for(int i=0; i<4; i++)
                {
                    pr.map_idx[i] = uint16_t((d + p - t.map)/2 + 1);
                    p += 2 + 2*size_t(read_u16(d + p));
                }
            }
            else
            for(int i=0; i<4; i++)
            {
                pr.map_idx[i] = uint16_t(d + p - t.map + 1);
                p += 1 + d[p];
            }
            if(p > size)
            {
                report(path, "bad DTZ map");
                return false;
            }
        }
        p += p & 1;
    }

    for(int f=0; f<t.files; f++)
    for(int s=0; s<t.sides; s++)
    {
        t.enc[f][s].pairs.sparse = d + p;
        p += sparse_bytes[f][s];
    }
    for(int f=0; f<t.files; f++)
    for(int s=0; s<t.sides; s++)
    {
        t.enc[f][s].pairs.block_len = d + p;
        p += len_bytes[f][s];
    }
    for(int f=0; f<t.files; f++)
    for(int s=0; s<t.sides; s++)
    {
        p = (p + 63) & ~size_t(63);
        t.enc[f][s].pairs.data = d + p;
        p += data_bytes[f][s];
    }
    if(p > size)
    {
        report(path, "file shorter than its header says");
        return false;
    }
    for(int f=0; f<t.files; f++)
    for(int s=0; s<t.sides; s++)
    {
        Pairs& pr = t.enc[f][s].pairs;
        if(!(pr.flags & PAIRS_SINGLE))
        pr.symlen = t.symlen.data() + (uintptr_t)pr.symlen;
    }
    return true;
}

// The parsed table of a mapped file, parsing it on first use; nullptr if it
// does not parse.
const Table* get_table(bool dtz, int index)
{
    Table_File& file = table_files[dtz][index];
    const Table* t = file.table.load(std::memory_order_acquire);
    if(t)
    return t;
    std::lock_guard<std::mutex> lock(parse_mutex);
    if((t = file.table.load(std::memory_order_relaxed)) || file.broken.load(std::memory_order_relaxed))
    return t;
    Table* parsed = new Table();
    parsed->dtz = dtz;
    int counts[12];
    parse_name(file.name, counts);
    const std::string path = table_dir + "/" + file.name + (dtz ? ".rtbz" : ".rtbw");
    if(!parse_table(*parsed, file.mapping, file.mapping_size, counts, path.c_str()))
    {
        delete parsed;
        file.broken.store(true, std::memory_order_relaxed);
        return nullptr;
    }
    file.table.store(parsed, std::memory_order_release);
    return parsed;
}

// The value at position `idx` of a stream.
int decompress(const Pairs& pr, uint64_t idx)
{
    if(pr.flags & PAIRS_SINGLE)
    return pr.single_value;

    // The sparse index entry covering idx records the block and offset of the
    // position in the middle of its range; walk from there to idx's block.
    uint64_t entry = idx >> pr.idx_bits;
    int64_t lit = int64_t(idx & ((1ULL << pr.idx_bits) - 1)) - (int64_t(1) << (pr.idx_bits - 1));
    uint32_t block = read_u32(pr.sparse + 6*entry);
    lit += read_u16(pr.sparse + 6*entry + 4);
    if(lit < 0)
    do
    {
        block--;
        lit += read_u16(pr.block_len + 2*block) + 1;
    }
    while(lit < 0);
    else
    while(lit > read_u16(pr.block_len + 2*block))
    {
        lit -= read_u16(pr.block_len + 2*block) + 1;
        block++;
    }

    // Decode symbols until the one whose run holds position lit of the block.
    const uint8_t* ptr = pr.data + (uint64_t(block) << pr.block_size);
    uint64_t code = read_be64(ptr);
    ptr += 8;
    int bits_used = 0;  // bits of `code` consumed since the last refill
    int sym;
    for(;;)
    {
        int l = 0;
        while(code < pr.base[l]) l++;
        sym = read_u16(pr.lowest_sym + 2*l) + int((code - pr.base[l]) >> (64 - (pr.min_len + l)));
        if(lit < pr.symlen[sym] + 1)
        break;
        lit -= pr.symlen[sym] + 1;
        int len = pr.min_len + l;
        code <<= len;
        bits_used += len;
        if(bits_used >= 32)
        {
            bits_used -= 32;
            code |= uint64_t(read_be32(ptr)) << bits_used;
            ptr += 4;
        }
    }

    // Descend the pair tree to the single value.
    while(pr.symlen[sym] != 0)
    {
        const uint8_t* w = pr.sympat + 3*sym;
        int left = (w[1] & 0xf) << 8 | w[0];
        if(lit < pr.symlen[left] + 1)
        sym = left;
        else
        {
            lit -= pr.symlen[left] + 1;
            sym = w[2] << 4 | w[1] >> 4;
        }
    }
    return pr.sympat[3*sym];
}

// Index of the pieces on sq[] (in the encoding's order, already coloured and,
// with pawns, flipped for the leading side) within the table.
uint64_t encode(const Table& t, const Encoding& e, int sq[MAX_PIECES])
{
    const int n = t.num;
    uint64_t idx;
    int i;
    if(t.has_pawns)
    {
        // Leading pawn on files a-d (pawn_file() put the one nearest the edge first).
        if(file_of(sq[0]) >= 4)
        for(int k=0; k<n; k++) sq[k] ^= 7;
        // The other leading pawns, highest pawn_twist first.
        for(int a=1; a<t.lead; a++)
        for(int b=a+1; b<t.lead; b++)
        if(pawn_twist[sq[a]] < pawn_twist[sq[b]])
        std::swap(sq[a], sq[b]);
        int m = t.lead - 1;
        idx = lead_pawn_idx[m][pawn_flap[sq[0]]];
        for(int a=1; a<t.lead; a++)
        idx += binom[m - a + 1][pawn_twist[sq[a]]];
        idx *= e.factor[0];
        i = t.lead;
        if(t.other_pawns)
        {
            // Squares a2..h7 (48) less the leading pawns'.
            int end = i + t.other_pawns;
            std::sort(sq + i, sq + end);
            uint64_t s = 0;
            for(int a=i; a<end; a++)
            {
                int below = 0;
                for(int b=0; b<i; b++) below += sq[a] > sq[b];
                s += binom[a - i + 1][sq[a] - below - 8];
            }
            idx += s * e.factor[i];
            i = end;
        }
    }
    else
    {
        // Leading piece into the a1-d1-d4 triangle, then the first leading
        // piece off the a1-h8 diagonal below it.
        if(file_of(sq[0]) >= 4)
        for(int k=0; k<n; k++) sq[k] ^= 7;
        if(rank_of(sq[0]) >= 4)
        for(int k=0; k<n; k++) sq[k] ^= 0x38;
        for(int k=0; k<t.lead; k++)
        {
            if(off_diag(sq[k]) == 0)
            continue;
            if(off_diag(sq[k]) > 0)
            for(int j=k; j<n; j++) sq[j] = flip_diag(sq[j]);
            break;
        }
        if(t.lead == 3)
        {
            int a1 = sq[1] > sq[0];
            int a2 = (sq[2] > sq[0]) + (sq[2] > sq[1]);
            if(off_diag(sq[0]))
            idx = (uint64_t(tri_idx[sq[0]]) * 63 + (sq[1] - a1)) * 62 + (sq[2] - a2);
            else if(off_diag(sq[1]))
            idx = 6*63*62 + (uint64_t(rank_of(sq[0])) * 28 + below_idx[sq[1]]) * 62 + (sq[2] - a2);
            else if(off_diag(sq[2]))
            idx = 6*63*62 + 4*28*62 + (uint64_t(rank_of(sq[0])) * 7 + (rank_of(sq[1]) - a1)) * 28 + below_idx[sq[2]];
            else
            idx = 6*63*62 + 4*28*62 + 4*7*28 + (uint64_t(rank_of(sq[0])) * 7 + (rank_of(sq[1]) - a1)) * 6 + (rank_of(sq[2]) - a2);
        }
        else
        idx = uint64_t(kk_idx[tri_idx[sq[0]]][sq[1]]);
        idx *= e.factor[0];
        i = t.lead;
    }

    // Every other group: a sorted set of squares among those the pieces before
    // it left free, in the combinatorial number system.
    while(i < n)
    {
        int end = i + e.group[i];
        std::sort(sq + i, sq + end);
        uint64_t s = 0;
        for(int a=i; a<end; a++)
        {
            int below = 0;
            for(int b=0; b<i; b++) below += sq[a] > sq[b];
            s += binom[a - i + 1][sq[a] - below];
        }
        idx += s * e.factor[i];
        i = end;
    }
    return idx;
}

// ---------------------------------------------------------------------------
// Probing a table
// ---------------------------------------------------------------------------

enum Probe_State { FAIL, OK, ZEROING_BEST, CHANGE_STM };

// The raw table value of pos: WDL 0..4, or the DTZ table's number (already
// through the map, in plies), or CHANGE_STM when the DTZ file holds the other
// side. wdl selects the DTZ map. rounded: the file stores this distance in
// moves, so the true one may be a ply longer.
int probe_table(const BB* const pos, bool dtz, int wdl, Probe_State& state, bool* rounded = nullptr)
{
    const uint64_t key = material_key(pos->Board);
    Key_Slot* slot = find_slot(key);
    if(!slot || slot->key != key || slot->table[dtz] < 0)
    {
        state = FAIL;
        return 0;
    }
    const Table* table = get_table(dtz, slot->table[dtz]);
    if(!table)
    {
        state = FAIL;
        return 0;
    }
    const Table& t = *table;

    // flip: the table's white is the board's black. A symmetric table is read
    // with the side to move as its white.
    const bool flip = t.symmetric ? !pos->white_move : slot->swapped;
    const int side = pos->white_move != flip ? 0 : 1;   // table side to move
    const int mirror = flip ? 0x38 : 0;                  // only with pawns

    int sq[MAX_PIECES];
    int n = 0, file = 0;
    const Encoding* e;
    if(t.has_pawns)
    {
        const int lead_code = t.enc[0][0].pieces[0];
        uint64_t bb = pos->Board[CODE_TO_BOARD[lead_code & 7] + 6*((lead_code >> 3) ^ flip)];
        while(bb)
        {
            sq[n++] = __builtin_ctzll(bb) ^ mirror;
            bb &= bb - 1;
        }
        // The leading pawn is the one nearest the edge (lowest flap), lower rank first.
        for(int k=1; k<n; k++)
        if(pawn_flap[sq[k]] < pawn_flap[sq[0]])
        std::swap(sq[0], sq[k]);
        file = file_of(sq[0]) <= 3 ? file_of(sq[0]) : 7 - file_of(sq[0]);
    }
    if(!dtz && side >= t.sides)
    {
        state = FAIL;
        return 0;
    }
    e = &t.enc[file][dtz ? 0 : side];
    if(dtz && (e->pairs.flags & PAIRS_STM) != side && !(t.symmetric && !t.has_pawns))
    {
        state = CHANGE_STM;
        return 0;
    }
    while(n < t.num)
    {
        const int code = e->pieces[n];
        uint64_t bb = pos->Board[CODE_TO_BOARD[code & 7] + 6*((code >> 3) ^ flip)];
        while(bb)
        {
            sq[n++] = __builtin_ctzll(bb) ^ mirror;
            bb &= bb - 1;
        }
    }
    int value = decompress(e->pairs, encode(t, *e, sq));
    state = OK;
    if(!dtz)
    return value;

    const Pairs& pr = e->pairs;
    if(pr.flags & PAIRS_MAPPED)
    {
        static const int WDL_TO_MAP[5] = {1, 3, 0, 2, 0};
        int at = pr.map_idx[WDL_TO_MAP[wdl + 2]] + value;
        value = pr.flags & PAIRS_WIDE ? read_u16(t.map + 2*at) : t.map[at];
    }
    // Distances stored in moves become plies.
    if((wdl == WDL_WIN && !(pr.flags & PAIRS_WIN_PLIES)) || (wdl == WDL_LOSS && !(pr.flags & PAIRS_LOSS_PLIES))
       || wdl == WDL_CURSED_WIN || wdl == WDL_BLESSED_LOSS)
    {
        value *= 2;
        if(rounded) *rounded = true;
    }
    return value + 1;
}

inline int piece_count(const BB* const pos)
{
    int n = 0;
    for(int i=0; i<12; i++)
    n += __builtin_popcountll(pos->Board[i]);
    return n;
}

inline bool is_capture(const BB* const pos, const Move& m)
{
    const uint64_t to = 1ULL << m.to;
    const int enemy = pos->white_move ? 6 : 0;
    for(int i=0; i<6; i++)
    if(pos->Board[i+enemy] & to)
    return true;
    // a pawn moving diagonally onto an empty square: en passant
    return (pos->Board[pos->white_move ? 0 : 6] >> m.from & 1) && file_of(m.from) != file_of(m.to);
}

inline bool is_pawn_move(const BB* const pos, const Move& m)
{
    return pos->Board[pos->white_move ? 0 : 6] >> m.from & 1;
}

// WDL of pos. Searches the captures (en passant included) and, with
// zeroing_moves, the pawn moves too, and uses the table only when none of
// them is at least as good as the stored value - where one is, the table may
// hold a "don't care". state becomes ZEROING_BEST when a searched move is the
// best one (then the DTZ table is not to be trusted either).
int search_wdl(const BB* const pos, bool zeroing_moves, Probe_State& state)
{
    if(piece_count(pos) == 2)
    {
        state = OK;
        return WDL_DRAW;
    }
    Move_List moves;
    const int total = generate_legal_moves(pos, moves, GEN_ALL);
    int best = WDL_LOSS, searched = 0;
    for(int k=0; k<total; k++)
    {
        const Move& m = moves[k];
        if(!is_capture(pos, m) && !(zeroing_moves && is_pawn_move(pos, m)))
        continue;
        searched++;
        BB child;
        make_move(pos, m, &child);
        int v = -search_wdl(&child, false, state);
        if(state == FAIL)
        return 0;
        if(v > best)
        {
            best = v;
            if(v >= WDL_WIN)
            {
                state = ZEROING_BEST;
                return v;
            }
        }
    }
    // Every legal move searched: the table has nothing to add (and with en
    // passant as the only move it would be wrong).
    const bool all_searched = searched && searched == total;
    int value;
    if(all_searched)
    value = best;
    else
    {
        value = probe_table(pos, false, 0, state) - 2;
        if(state == FAIL)
        return 0;
    }
    if(best >= value)
    {
        state = best > WDL_DRAW || all_searched ? ZEROING_BEST : OK;
        return best;
    }
    state = OK;
    return value;
}

// DTZ of the move just before a zeroing move into a position of value wdl.
inline int dtz_before_zeroing(int wdl)
{
    return wdl == WDL_WIN ? 1 : wdl == WDL_CURSED_WIN ? 101 : wdl == WDL_BLESSED_LOSS ? -101 : wdl == WDL_LOSS ? -1 : 0;
}

inline int sign(int v) { return (v > 0) - (v < 0); }

// DTZ of pos. depth guards the 1-ply search: its children are the stored side.
// rounded: the true DTZ may be one ply longer (see probe_table()).
int search_dtz(const BB* const pos, Probe_State& state, int depth, bool& rounded)
{
    rounded = false;
    const int wdl = search_wdl(pos, true, state);
    if(state == FAIL || wdl == WDL_DRAW)
    return 0;
    if(state == ZEROING_BEST)
    return dtz_before_zeroing(wdl);

    int dtz = probe_table(pos, true, wdl, state, &rounded);
    if(state == FAIL)
    return 0;
    if(state != CHANGE_STM)
    return (dtz + 100*(wdl == WDL_CURSED_WIN || wdl == WDL_BLESSED_LOSS)) * sign(wdl);
    if(depth > 0)
    {
        state = FAIL;
        return 0;
    }

    // The file holds the other side to move: take the best move by its children,
    // the fastest win or the slowest loss.
    Move_List moves;
    const int total = generate_legal_moves(pos, moves, GEN_ALL);
    int best = 0xffff;
    bool best_precise = false, best_rounded = false;  // some move reaching best is / is not rounded
    for(int k=0; k<total; k++)
    {
        const Move& m = moves[k];
        const bool zeroing = is_capture(pos, m) || is_pawn_move(pos, m);
        BB child;
        make_move(pos, m, &child);
        bool r = false;
        int v;
        if(zeroing)   // counted from before the move: the child's own DTZ restarts
        v = -dtz_before_zeroing(search_wdl(&child, false, state));
        else
        {
            v = -search_dtz(&child, state, depth + 1, r);
            if(!(v == 1 && child.get_in_check() && count_legal_moves(&child, 1) == 0))  // not mate
            v += sign(v);
        }
        if(state == FAIL)
        return 0;
        if(sign(v) != sign(wdl))
        continue;
        if(v < best)
        {
            best = v;
            best_precise = !r;
            best_rounded = r;
        }
        else if(v == best)
        {
            best_precise |= !r;
            best_rounded |= r;
        }
    }
    state = OK;
    if(best == 0xffff)
    return -1;
    // A rounded move may be a ply longer than it says: that only matters when it
    // could be the best one, i.e. winning, no precise move is as fast; losing,
    // it is among the slowest.
    rounded = wdl > 0 ? !best_precise : best_rounded;
    return best;
}

bool probeable(const BB* const pos)
{
    if(pos->castle[0][0] || pos->castle[0][1] || pos->castle[1][0] || pos->castle[1][1])
    return false;
    return piece_count(pos) <= largest;
}

} // namespace detail

void release()
{
    using namespace detail;
    std::lock_guard<std::mutex> lock(parse_mutex);
    for(int dtz=0; dtz<2; dtz++)
    {
        for(int i=0; i<table_count[dtz]; i++)
        {
            Table_File& file = table_files[dtz][i];
            delete file.table.load();
            file.table.store(nullptr);
            file.broken.store(false);
            if(file.mapping)
            munmap((void*)file.mapping, file.mapping_size);
            file.mapping = nullptr;
            file.mapping_size = 0;
            file.name[0] = 0;
        }
        table_count[dtz] = 0;
    }
    for(Key_Slot& s : key_slots)
    s = Key_Slot{0, {-1, -1}, false};
    largest = 0;
}

int init(const std::string& dir)
{
    using namespace detail;
    init_index_tables();
    release();
    table_dir = dir;

    DIR* dp = opendir(dir.c_str());
    if(!dp)
    {
        std::fprintf(stderr, "syzygy: cannot open %s\n", dir.c_str());
        return 0;
    }
    std::vector<std::string> names;
    while(dirent* de = readdir(dp))
    names.push_back(de->d_name);
    closedir(dp);
    std::sort(names.begin(), names.end());

    for(const std::string& file : names)
    {
        if(file.size() < 6)
        continue;
        std::string ext = file.substr(file.size()-5), stem = file.substr(0, file.size()-5);
        if(ext != ".rtbw" && ext != ".rtbz")
        continue;
        const bool dtz = ext == ".rtbz";
        int counts[12];
        if(stem.size() >= sizeof(Table_File::name) || !parse_name(stem.c_str(), counts))
        continue;
        int num = 0;
        for(int c : counts) num += c;
        if(num > MAX_PIECES)
        continue;
        if(table_count[dtz] == MAX_TABLES)
        {
            std::fprintf(stderr, "syzygy: more than %d tables, the rest are skipped\n", MAX_TABLES);
            break;
        }

        const std::string path = dir + "/" + file;
        int fd = open(path.c_str(), O_RDONLY);
        if(fd < 0)
        continue;
        struct stat st;
        if(fstat(fd, &st) != 0 || st.st_size <= 0)
        {
            close(fd);
            continue;
        }
        void* m = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        if(m == MAP_FAILED)
        {
            report(path.c_str(), "mmap failed");
            continue;
        }
        // Probes jump around the file; reading ahead would only fill memory.
        madvise(m, size_t(st.st_size), MADV_RANDOM);

        Table_File& tf = table_files[dtz][table_count[dtz]];
        std::strcpy(tf.name, stem.c_str());
        tf.mapping = (const uint8_t*)m;
        tf.mapping_size = size_t(st.st_size);

        // Register the material both ways round (once if it is the same both ways).
        bool symmetric = true;
        for(int i=0; i<6; i++)
        symmetric &= counts[i] == counts[i+6];
        for(int swapped=0; swapped<2; swapped++)
        {
            uint64_t key = 0;
            for(int i=0; i<12; i++)
            key |= uint64_t(counts[swapped ? (i+6) % 12 : i]) << (4*i);
            Key_Slot* slot = find_slot(key);
            if(!slot)
            break;
            if(slot->key == 0)
            *slot = Key_Slot{key, {-1, -1}, bool(swapped)};
            if(slot->swapped == bool(swapped))
            slot->table[dtz] = int16_t(table_count[dtz]);
            if(symmetric)
            break;
        }
        table_count[dtz]++;
        if(!dtz && num > largest)
        largest = num;
    }
    return table_count[0];
}

int max_pieces() { return detail::largest; }
int wdl_table_count() { return detail::table_count[0]; }
int dtz_table_count() { return detail::table_count[1]; }

bool probe_wdl(const BB* const pos, int& wdl)
{
    using namespace detail;
    if(!probeable(pos))
    return false;
    Probe_State state;
    int v = search_wdl(pos, false, state);
    if(state == FAIL)
    return false;
    wdl = v;
    return true;
}

bool probe_dtz(const BB* const pos, int& dtz, bool* rounded)
{
    using namespace detail;
    if(!probeable(pos))
    return false;
    Probe_State state;
    bool r = false;
    int v = search_dtz(pos, state, 0, r);
    if(state == FAIL)
    return false;
    dtz = v;
    if(rounded) *rounded = r;
    return true;
}

} // namespace syzygy

#endif // SYZYGY_CPP
