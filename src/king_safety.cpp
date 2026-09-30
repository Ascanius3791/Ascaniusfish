// OWNERSHIP=Claude
#ifndef KING_SAFETY_CPP
#define KING_SAFETY_CPP
#include "../lib/king_safety.hpp"

// Attack half. Danger units per attacker by piece index (pawn, rook, knight,
// bishop, queen, king); a queen is mostly counted through its checks instead.
constexpr int KS_ATTACKER_WEIGHT[6] = {0, 44, 81, 52, 10, 0};
constexpr int KS_MIN_ATTACKERS = 2;       // fewer ring attackers than this: no attack danger
constexpr int KS_HIT = 69;                // per attack on a square next to the king
constexpr int KS_WEAK_RING_SQUARE = 185;  // ring square attacked by them, covered by us at most by king/queen
constexpr int KS_SAFE_CHECK[6] = {0, 1080, 790, 635, 780, 0};
constexpr int KS_UNSAFE_CHECK = 148;      // a check the enemy has, but onto a square we cover
constexpr int KS_NO_QUEEN = 873;          // taken off the danger when the enemy has no queen
constexpr int KS_DANGER_DIV = 5000;       // penalty = danger^2 / this, in centipawns

// Shelter half, per file of the three in front of the king, by the relative rank
// (2..7) of the rearmost own pawn on or ahead of the king's rank; index 0 = none.
constexpr int KS_SHELTER[8] = {40, 40, 0, 12, 28, 38, 38, 38};
constexpr int KS_OPEN_FILE = 20;          // no pawn of either colour on it
constexpr int KS_STORM[8] = {0, 0, 0, 30, 15, 6, 0, 0};  // enemy pawn at this relative rank
constexpr int KS_STORM_BLOCKED_DIV = 4;   // storm pawn stopped right in front of our pawn
constexpr int KS_FULL_PIECE_MATERIAL = 2*3 + 2*3 + 2*5 + 9;  // N, B, R, Q in pawns

struct KS_Maps
{
    uint64_t all[2] = {0, 0};     // [1] = white
    uint64_t twice[2] = {0, 0};   // attacked at least twice
    uint64_t by_type[2][6] = {};  // by piece index
    int attackers[2] = {0, 0};    // on the ring of [c]'s king
    int weight[2] = {0, 0};       // their summed KS_ATTACKER_WEIGHT
    int hits[2] = {0, 0};         // their attacks on squares next to [c]'s king
};

static inline void ks_add(KS_Maps& m, int c, uint64_t a)
{
    m.twice[c] |= m.all[c] & a;
    m.all[c] |= a;
}

static inline uint64_t ks_ring(int ksq)
{
    int file = ksq % 8, rank = ksq / 8;
    file = file < 1 ? 1 : file > 6 ? 6 : file;
    rank = rank < 1 ? 1 : rank > 6 ? 6 : rank;
    int centre = rank * 8 + file;
    return K_template[centre] | 1ULL << centre;
}

static void ks_attack_maps(const BB* const original, KS_Maps& m, const int ksq[2], const uint64_t ring[2])
{
    const uint64_t* B = original->Board;
    uint64_t occ = original->get_occupancy();
    for(int c = 0; c < 2; c++)
    {
        int off = 6 * !c;
        uint64_t p = B[0 + off];
        uint64_t left  = c ? (p & ~mask_column[0]) << 7 : (p & ~mask_column[0]) >> 9;
        uint64_t right = c ? (p & ~mask_column[7]) << 9 : (p & ~mask_column[7]) >> 7;
        m.by_type[c][0] = left | right;
        ks_add(m, c, left);
        ks_add(m, c, right);

        int e = !c;  // the king these pieces attack
        uint64_t enemy_king_adjacent = K_template[ksq[e]];
        for(int piece = 1; piece < 5; piece++)
        {
            uint64_t pieces = B[piece + off];
            while(pieces)
            {
                int sq = find_and_delete_trailling_1(pieces);
                uint64_t a;
                if(piece == 2)      a = Kn_template[sq];
                else if(piece == 3) a = get_bishop_attacks(sq, occ);
                else if(piece == 1) a = get_rook_attacks(sq, occ);
                else                a = get_bishop_attacks(sq, occ) | get_rook_attacks(sq, occ);
                m.by_type[c][piece] |= a;
                ks_add(m, c, a);
                if(a & ring[e])
                {
                    m.attackers[e]++;
                    m.weight[e] += KS_ATTACKER_WEIGHT[piece];
                    m.hits[e] += count(a & enemy_king_adjacent);
                }
            }
        }
        m.by_type[c][5] = K_template[ksq[c]];
        ks_add(m, c, K_template[ksq[c]]);
    }
}

// Attack penalty for side `d`'s king, once the maps are complete.
static int ks_attack_penalty(const BB* const original, const KS_Maps& m, int d, int ksq, uint64_t ring, int& units_out)
{
    const uint64_t* B = original->Board;
    int t = !d;
    // The attacker count times their summed weight: it is the joining that makes an attack.
    int units = m.attackers[d] * m.weight[d] + KS_HIT * m.hits[d];
    units_out = units;
    if(m.attackers[d] < KS_MIN_ATTACKERS)
    return 0;

    uint64_t weak = m.all[t] & ~m.twice[d] & (~m.all[d] | m.by_type[d][5] | m.by_type[d][4]);
    units += KS_WEAK_RING_SQUARE * count(ring & weak);

    uint64_t their_pieces = original->get_pieces_of_colour(t);
    uint64_t safe = ~their_pieces & (~m.all[d] | (weak & m.twice[t]));
    uint64_t occ = original->get_occupancy();
    uint64_t rook_lines = get_rook_attacks(ksq, occ);
    uint64_t bishop_lines = get_bishop_attacks(ksq, occ);
    uint64_t checks[6] = {0, rook_lines, Kn_template[ksq], bishop_lines, rook_lines | bishop_lines, 0};
    for(int piece = 1; piece < 5; piece++)
    {
        uint64_t from = checks[piece] & m.by_type[t][piece] & ~their_pieces;
        if(from & safe)
        units += KS_SAFE_CHECK[piece];
        else if(from)
        units += KS_UNSAFE_CHECK;
    }
    if(!B[4 + 6 * !t])
    units -= KS_NO_QUEEN;

    units_out = units;
    if(units <= 0)
    return 0;
    return units * units / KS_DANGER_DIV;
}

// Shelter penalty for side `d`'s king, scaled by the enemy's piece material.
static int ks_shelter_penalty(const BB* const original, int d, int ksq)
{
    const uint64_t* B = original->Board;
    int off = 6 * !d, enemy_off = 6 * d;
    int king_rank = ksq / 8;
    // Pawns on the king's rank or ahead of it (towards the enemy).
    uint64_t ahead = d ? ~0ULL << (8 * king_rank)
                       : (king_rank == 7 ? ~0ULL : (1ULL << (8 * (king_rank + 1))) - 1);
    uint64_t own_pawns = B[0 + off] & ahead;
    uint64_t their_pawns = B[0 + enemy_off] & ahead;
    uint64_t all_pawns = B[0] | B[6];

    int centre = ksq % 8;
    centre = centre < 1 ? 1 : centre > 6 ? 6 : centre;
    int penalty = 0;
    for(int f = centre - 1; f <= centre + 1; f++)
    {
        uint64_t own = own_pawns & mask_column[f];
        uint64_t their = their_pawns & mask_column[f];
        // rearmost own pawn, and the enemy pawn nearest to us, as relative ranks 1..8
        int own_rank = 0, their_rank = 0;
        if(own)
        own_rank = d ? __builtin_ctzll(own) / 8 + 1 : 8 - (63 - __builtin_clzll(own)) / 8;
        if(their)
        their_rank = d ? __builtin_ctzll(their) / 8 + 1 : 8 - (63 - __builtin_clzll(their)) / 8;

        penalty += KS_SHELTER[own_rank];
        if(!(all_pawns & mask_column[f]))
        penalty += KS_OPEN_FILE;
        int storm = KS_STORM[their_rank];
        if(own_rank && own_rank == their_rank - 1)
        storm /= KS_STORM_BLOCKED_DIV;
        penalty += storm;
    }

    int material = 3 * count(B[2 + enemy_off]) + 3 * count(B[3 + enemy_off])
                 + 5 * count(B[1 + enemy_off]) + 9 * count(B[4 + enemy_off]);
    if(material > KS_FULL_PIECE_MATERIAL)
    material = KS_FULL_PIECE_MATERIAL;
    return penalty * material / KS_FULL_PIECE_MATERIAL;
}

static void ks_prepare(const BB* const original, KS_Maps& m, int ksq[2], uint64_t ring[2])
{
    ksq[1] = __builtin_ctzll(original->Board[5]);
    ksq[0] = __builtin_ctzll(original->Board[11]);
    ring[1] = ks_ring(ksq[1]);
    ring[0] = ks_ring(ksq[0]);
    ks_attack_maps(original, m, ksq, ring);
}

King_Safety_Detail king_safety_detail(const BB* const original, bool white)
{
    KS_Maps m;
    int ksq[2];
    uint64_t ring[2];
    ks_prepare(original, m, ksq, ring);
    King_Safety_Detail r;
    int d = white;
    r.attackers = m.attackers[d];
    r.attack_penalty = ks_attack_penalty(original, m, d, ksq[d], ring[d], r.danger_units);
    r.shelter_penalty = ks_shelter_penalty(original, d, ksq[d]);
    return r;
}

void king_safety_of_both(const BB* const original, int& white, int& black)
{
    KS_Maps m;
    int ksq[2];
    uint64_t ring[2];
    ks_prepare(original, m, ksq, ring);
    int units;
    white = -(ks_attack_penalty(original, m, 1, ksq[1], ring[1], units) + ks_shelter_penalty(original, 1, ksq[1]));
    black = -(ks_attack_penalty(original, m, 0, ksq[0], ring[0], units) + ks_shelter_penalty(original, 0, ksq[0]));
}

int king_safety_eval(const BB* const original)
{
    int white, black;
    king_safety_of_both(original, white, black);
    return white - black;
}

#endif // KING_SAFETY_CPP
