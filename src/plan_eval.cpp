// OWNERSHIP=Claude
#ifndef PLAN_EVAL_CPP
#define PLAN_EVAL_CPP
#include "../lib/plan_eval.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>

int plan_partial_eval(const BB* const original, const WEIGHTS& W)
{
    return piecetable(original,W) + piece_activity_eval(original,W) + positional_eval(original,W);
}

// Squares one move of a `type` piece (0..5) of colour `white` on `sq` reaches,
// with `occ` the other pieces. Pawns: pushes only.
static inline uint64_t plan_step(int type, bool white, int sq, uint64_t occ)
{
    switch(type)
    {
        case 0:
        {
            const int s = white ? sq+8 : sq-8;
            if(s < 0 || s > 63 || (occ >> s & 1))
            return 0;
            uint64_t to = 1ULL << s;
            const bool start = white ? (sq >= 8 && sq < 16) : (sq >= 48 && sq < 56);
            const int d = white ? sq+16 : sq-16;
            if(start && !(occ >> d & 1))
            to |= 1ULL << d;
            return to;
        }
        case 1: return get_rook_attacks(sq,occ);
        case 2: return Kn_template[sq];
        case 3: return get_bishop_attacks(sq,occ);
        case 4: return get_rook_attacks(sq,occ) | get_bishop_attacks(sq,occ);
        default: return K_template[sq];
    }
}

// avoid[type]: the squares a `type` piece of colour `white` never passes
// through or stops on. Pawns: the first and last rank. Others: squares an
// enemy pawn attacks, and with Q4 also those an enemy piece of lower value
// attacks (every enemy piece for the king).
static void plan_avoid(const uint64_t* Board, uint64_t occ, bool white, uint64_t* avoid)
{
    const uint64_t* e = Board + (white ? 6 : 0);
    uint64_t att[6] = {};
    for(uint64_t b = e[0]; b; )
    {
        const int i = find_and_delete_trailling_1(b);
        att[0] |= white ? WP_template[i] : BP_template[i];
    }
    if(PLAN_Q4_AVOID_LOWER)
    {
        for(int t=1;t<6;t++)
        for(uint64_t b = e[t]; b; )
        {
            const int i = find_and_delete_trailling_1(b);
            att[t] |= t == 2 || t == 5 ? plan_step(t, !white, i, occ) : (t != 3 ? get_rook_attacks(i,occ) : 0) | (t != 1 ? get_bishop_attacks(i,occ) : 0);
        }
    }
    avoid[0] = mask_row[0] | mask_row[7];
    avoid[2] = avoid[3] = att[0];
    avoid[1] = att[0] | att[2] | att[3];
    avoid[4] = avoid[1] | att[1];
    avoid[5] = avoid[4] | att[4] | att[5];
}

// ---- db without re-evaluating the board ------------------------------------
// The gain of moving one piece s -> t is the difference of the three partial
// evals, each computed exactly (the same integers, the same truncations):
// - piecetable() is a sum in 39ths divided once, so only the piece's two
//   table entries change the sum;
// - pawn_struckture_eval_of_colour(): a push keeps the pawn's file, so only
//   the "pawn supports a piece" counts at s and t change;
// - piece_activity_eval() is a sum of one term per piece role (a queen has a
//   diagonal and an orthogonal one) divided by 2 at the end. The moved
//   piece's roles are replaced by the same roles at t. Every other role's
//   term is linear in the pieces inside its mask:
//   - a leaper's or pawn's mask is fixed, so a piece of colour c on x adds the
//     same to it whatever else stands where. leaper_gain[c][x] sums that over
//     all leaper roles, once per call; moving s -> t adds
//     leaper_gain[c][t] - leaper_gain[c][s] (the moved piece's own role is
//     taken out of the table while that piece is planned);
//   - a slider's mask is its attacks. Emptying s lengthens the rays of the
//     sliders that see s; those are recomputed once per piece, since all of
//     its targets share an empty s. Occupying t then cuts every ray that
//     reaches t: the slider loses its attacks beyond t (plan_beyond), and t
//     turns from an empty attacked square into a piece of colour c.
// PLAN_REFERENCE_DB recomputes the partial evals instead (the definition);
// diagnostics/plan_eval_probe.cpp checks the two agree everywhere.
// PLAN_OWN_ACT keeps only the moved piece's roles: no leaper_gain, slider_gain
// or vacated sliders, and plan_setup() builds none of their tables.

enum Plan_Role_Kind { ROLE_PAWN, ROLE_DIAG, ROLE_ORTH, ROLE_KNIGHT, ROLE_KING };

// piece_activity_eval()'s weights. A non-pawn role: per piece of its own
// colour in its mask, per enemy piece in its mask, per square of the mask.
constexpr int PLAN_ROLE_W[5][3] = {{0,0,0}, {10,40,5}, {-10,40,7}, {-10,10,5}, {15,20,0}};
// A pawn: per own/enemy piece it attacks, per own/enemy piece it would attack
// after a push (single, or double from the start rank), and any piece in front of it.
constexpr int PLAN_PAWN_ATT_OWN = 15, PLAN_PAWN_ATT_ENEMY = 30;
constexpr int PLAN_PAWN_PUSH_OWN = 10, PLAN_PAWN_PUSH_ENEMY = 20;
constexpr int PLAN_PAWN_BLOCKED = -20;

static inline uint64_t plan_pawn_att(bool white, int i)
{
    return white ? BP_template[i] : WP_template[i];
}

static inline uint64_t plan_pawn_push_att(bool white, int i)
{
    return white ? (BP_template[i]<<8 | (i/8==1 ? BP_template[i]<<16 : 0))
                 : (WP_template[i]>>8 | (i/8==6 ? WP_template[i]>>16 : 0));
}

// A non-pawn role's term with mask m, white's view.
static inline int plan_masked_term(int kind, bool white, uint64_t m, uint64_t own, uint64_t enemy)
{
    const int* w = PLAN_ROLE_W[kind];
    const int t = w[0]*count(own & m) + w[1]*count(enemy & m) + w[2]*count(m);
    return white ? t : -t;
}

// One role's piece_activity_eval() term, white's view; *mask gets what it reads.
static inline int plan_role_term(int kind, bool white, int i, uint64_t occ, uint64_t white_pieces, uint64_t black_pieces, uint64_t* mask)
{
    const uint64_t own = white ? white_pieces : black_pieces;
    const uint64_t enemy = white ? black_pieces : white_pieces;
    switch(kind)
    {
        case ROLE_PAWN:
        {
            const uint64_t att = plan_pawn_att(white,i);
            const uint64_t front = white ? 1ULL << i+8 : 1ULL << i >> 8;
            const uint64_t side = plan_pawn_push_att(white,i);
            *mask = att | front | side;
            int t = count(enemy & att)*PLAN_PAWN_ATT_ENEMY + count(own & att)*PLAN_PAWN_ATT_OWN
                  + count(enemy & side)*PLAN_PAWN_PUSH_ENEMY + count(own & side)*PLAN_PAWN_PUSH_OWN;
            if(occ & front)
            t += PLAN_PAWN_BLOCKED;
            return white ? t : -t;
        }
        case ROLE_DIAG: *mask = get_bishop_attacks(i,occ); break;
        case ROLE_ORTH: *mask = get_rook_attacks(i,occ); break;
        case ROLE_KNIGHT: *mask = Kn_template[i]; break;
        default: *mask = K_template[i]; break;
    }
    return plan_masked_term(kind, white, *mask, own, enemy);
}

// Adds f times what a piece on each square of a leaper role's mask adds to
// that role's term (white's view) to gain[colour of the piece][square].
static inline void plan_add_leaper(int (*gain)[64], int kind, bool white, int i, int f)
{
    const int sg = white ? f : -f;
    if(kind == ROLE_PAWN)
    {
        uint64_t m = plan_pawn_att(white,i);
        while(m)
        {
            const int x = find_and_delete_trailling_1(m);
            gain[white][x] += sg*PLAN_PAWN_ATT_OWN;
            gain[!white][x] += sg*PLAN_PAWN_ATT_ENEMY;
        }
        m = plan_pawn_push_att(white,i);
        while(m)
        {
            const int x = find_and_delete_trailling_1(m);
            gain[white][x] += sg*PLAN_PAWN_PUSH_OWN;
            gain[!white][x] += sg*PLAN_PAWN_PUSH_ENEMY;
        }
        const int x = white ? i+8 : i-8;
        gain[0][x] += sg*PLAN_PAWN_BLOCKED;
        gain[1][x] += sg*PLAN_PAWN_BLOCKED;
        return;
    }
    const int* w = PLAN_ROLE_W[kind];
    uint64_t m = kind == ROLE_KNIGHT ? Kn_template[i] : K_template[i];
    while(m)
    {
        const int x = find_and_delete_trailling_1(m);
        gain[white][x] += sg*w[0];
        gain[!white][x] += sg*w[1];
    }
}

// Roles of a `type` piece (0..5): up to two.
static inline int plan_roles_of(int type, int* kinds)
{
    switch(type)
    {
        case 0: kinds[0] = ROLE_PAWN; return 1;
        case 1: kinds[0] = ROLE_ORTH; return 1;
        case 2: kinds[0] = ROLE_KNIGHT; return 1;
        case 3: kinds[0] = ROLE_DIAG; return 1;
        case 4: kinds[0] = ROLE_DIAG; kinds[1] = ROLE_ORTH; return 2;
        default: kinds[0] = ROLE_KING; return 1;
    }
}

// plan_beyond.sq[a][b]: the squares on the line from a through b that lie
// beyond b; 0 when a and b share no rank, file or diagonal.
struct Plan_Beyond { uint64_t sq[64][64]; };

static constexpr Plan_Beyond plan_make_beyond()
{
    Plan_Beyond t{};
    for(int a=0;a<64;a++)
    for(int dr=-1;dr<=1;dr++)
    for(int df=-1;df<=1;df++)
    {
        if(!dr && !df)
        continue;
        int line[8] = {};
        int n = 0;
        for(int r=a/8+dr, f=a%8+df; r>=0 && r<8 && f>=0 && f<8; r+=dr, f+=df)
        line[n++] = 8*r+f;
        for(int k=0;k<n;k++)
        for(int j=k+1;j<n;j++)
        t.sq[a][line[k]] |= 1ULL << line[j];
    }
    return t;
}

static constexpr Plan_Beyond plan_beyond = plan_make_beyond();

// A slider role (on sq, attacks a) sees the empty square x. What its term
// (its own view) loses when a piece lands on x and it no longer sees past it.
// sides[1] = white's pieces.
static inline int plan_lost(int kind, bool rw, int sq, int x, uint64_t a, const uint64_t* sides)
{
    const int* w = PLAN_ROLE_W[kind];
    const uint64_t lost = a & plan_beyond.sq[sq][x];
    return w[0]*count(sides[rw] & lost) + w[1]*count(sides[!rw] & lost) + w[2]*count(lost);
}

// What a piece of colour c landing there adds to the role's term, white's view.
static inline int plan_cut(int kind, bool rw, bool c, int lost)
{
    const int g = (rw == c ? PLAN_ROLE_W[kind][0] : PLAN_ROLE_W[kind][1]) - lost;
    return rw ? g : -g;
}

constexpr bool PLAN_REFERENCE_DB_DEFAULT = false;

// Everything plan_piece() reads that does not depend on the moved piece.
struct Plan_Ctx
{
    const BB* original;
    const WEIGHTS* W;
    int base;// reference mode: plan_partial_eval() of the position (PLAN_OWN_ACT: without the activity)
    int ref_act;// reference mode, PLAN_OWN_ACT: plan_ref_activity() of every piece, summed
    uint64_t side_pieces[2];// [1] = white
    uint64_t occupancy;
    uint64_t avoid[2][6];// plan_avoid(), [1] = white's pieces
    int OW[2], EW[2];// piecetable()'s phase weights; [1] = white's: the enemy (black) material left
    int pt39;// piecetable() in 39ths
    int act;// piece_activity_eval() before its /2
    int term_at[64];// the role terms of the piece on a square
    int leaper_gain[2][64];// [colour of a piece on the square]: what the leaper roles gain from it
    int slider_gain[2][64];// plan_cut() of every slider seeing the square, by the colour landing there
    int n_sl;
    int sl_kind[48], sl_sq[48], sl_term[48];
    bool sl_white[48];
    uint64_t sl_att[48];// attacks, with the planned piece's square emptied
    uint64_t sl_att0[48];// attacks in the position
    uint64_t sl_on[64];// bit r: slider role r belongs to the piece on the square
    uint64_t sl_seeing[64];// bit r: slider role r attacks the square
    int sl_lost[48][64];// plan_lost(), set where sl_att0[r] has the square and it is empty
    BB* nne_board;// with the net (#67): a copy of the position, one piece moved at a time; nullptr without
    double nne_base;// the net's correction of the position, white's view
};

// The net's correction (lib/nne.hpp, mover's view) in white's view.
static inline double plan_nne_white(const BB& b)
{
    const double c = nne::correction(b);
    return b.white_move ? c : -c;
}

// Reference for PLAN_OWN_ACT: the raw piece_activity_eval() terms (before its
// /2, white's view) of the piece in Board[p] on sq, written from
// piece_activity_eval() itself rather than from the role tables above.
static int plan_ref_activity(const BB* const b, int p, int sq)
{
    uint64_t white_pieces = 0, black_pieces = 0;
    for(int q=0;q<6;q++)
    {
        white_pieces |= b->Board[q];
        black_pieces |= b->Board[q+6];
    }
    const uint64_t all_pieces = white_pieces | black_pieces;
    const bool col = p < 6;
    const uint64_t own_pieces = col ? white_pieces : black_pieces;
    const uint64_t enemy_pieces = col ? black_pieces : white_pieces;
    const int sg = col ? 1 : -1;
    const int type = p % 6;
    int score = 0;
    if(type == 0)
    {
        const uint64_t att = col ? BP_template[sq] : WP_template[sq];
        score += sg*(count(enemy_pieces & att)*30 + count(own_pieces & att)*15);
        if(all_pieces & (col ? 1ULL << sq+8 : 1ULL << sq >> 8))
        score -= sg*20;
        const uint64_t push_attacks = col ? (BP_template[sq]<<8 | (sq/8==1 ? BP_template[sq]<<16 : 0)) : (WP_template[sq]>>8 | (sq/8==6 ? WP_template[sq]>>16 : 0));
        score += sg*(count(enemy_pieces & push_attacks)*20 + count(own_pieces & push_attacks)*10);
    }
    if(type == 3 || type == 4)
    {
        const uint64_t a = get_bishop_attacks(sq,all_pieces);
        score += sg*(count(own_pieces & a)*10 + count(enemy_pieces & a)*40 + count(a)*5);
    }
    if(type == 1 || type == 4)
    {
        const uint64_t a = get_rook_attacks(sq,all_pieces);
        score += sg*(-count(own_pieces & a)*10 + count(enemy_pieces & a)*40 + count(a)*7);
    }
    if(type == 2)
    score += sg*(-count(own_pieces & Kn_template[sq])*10 + count(enemy_pieces & Kn_template[sq])*10 + count(Kn_template[sq])*5);
    if(type == 5)
    score += sg*(count(own_pieces & K_template[sq])*15 + count(enemy_pieces & K_template[sq])*20);
    return score;
}

// The sum of plan_ref_activity() over all pieces: piece_activity_eval() before its /2.
static int plan_ref_activity_all(const BB* const b)
{
    int sum = 0;
    for(int p=0;p<12;p++)
    for(uint64_t bb = b->Board[p]; bb; )
    sum += plan_ref_activity(b, p, find_and_delete_trailling_1(bb));
    return sum;
}

static void plan_setup(Plan_Ctx& c, const BB* const original, const WEIGHTS& W)
{
    const uint64_t* Board = original->Board;
    c.original = original;
    c.W = &W;
    c.base = 0;
    c.side_pieces[0] = c.side_pieces[1] = 0;
    for(int p=0;p<6;p++)
    {
        c.side_pieces[1] |= Board[p];
        c.side_pieces[0] |= Board[p+6];
    }
    c.occupancy = c.side_pieces[0] | c.side_pieces[1];
    for(int col=0;col<2;col++)
    plan_avoid(Board, c.occupancy, col, c.avoid[col]);

    for(int col=0;col<2;col++)
    {
        const int e = 6*col;// col 1 (white) counts black's Board[6..]
        c.OW[col] = count(Board[0+e]) + 5*count(Board[1+e]) + 3*count(Board[2+e]) + 3*count(Board[3+e]) + 9*count(Board[4+e]);
        c.EW[col] = 39 - c.OW[col];
    }
    c.pt39 = 0;
    for(int p=0;p<12;p++)
    {
        const bool white = p < 6;
        const int row = white ? p : (p==6 ? 6 : p-6);
        uint64_t bb = Board[p];
        while(bb)
        {
            const int i = find_and_delete_trailling_1(bb);
            const int v = W.piece_table_value_opening[row][i]*c.OW[white] + W.piece_table_value_endgame[row][i]*c.EW[white];
            c.pt39 += white ? v : -v;
        }
    }

    // piece_activity_eval() by role: sliders one by one, leapers as a table
    c.act = 0;
    c.n_sl = 0;
    if(!PLAN_OWN_ACT)
    {
        std::memset(c.leaper_gain, 0, sizeof(c.leaper_gain));
        std::memset(c.slider_gain, 0, sizeof(c.slider_gain));
        std::memset(c.sl_seeing, 0, sizeof(c.sl_seeing));
    }
    for(int p=0;p<12;p++)
    {
        const bool white = p < 6;
        int kinds[2];
        const int nk = plan_roles_of(p % 6, kinds);
        uint64_t bb = Board[p];
        while(bb)
        {
            const int i = find_and_delete_trailling_1(bb);
            c.term_at[i] = 0;
            c.sl_on[i] = 0;
            for(int k=0;k<nk;k++)
            {
                uint64_t mask;
                const int term = plan_role_term(kinds[k], white, i, c.occupancy, c.side_pieces[1], c.side_pieces[0], &mask);
                c.term_at[i] += term;
                c.act += term;
                if(PLAN_OWN_ACT)// the other pieces' roles are not needed
                continue;
                if(kinds[k] == ROLE_DIAG || kinds[k] == ROLE_ORTH)
                {
                    const int r = c.n_sl++;
                    c.sl_kind[r] = kinds[k];
                    c.sl_sq[r] = i;
                    c.sl_white[r] = white;
                    c.sl_att[r] = c.sl_att0[r] = mask;
                    c.sl_term[r] = term;
                    c.sl_on[i] |= 1ULL << r;
                    for(uint64_t m = mask & c.occupancy; m; )
                    c.sl_seeing[find_and_delete_trailling_1(m)] |= 1ULL << r;
                    for(uint64_t m = mask & ~c.occupancy; m; )// only empty squares are targets
                    {
                        const int x = find_and_delete_trailling_1(m);
                        c.sl_seeing[x] |= 1ULL << r;
                        const int lost = c.sl_lost[r][x] = plan_lost(kinds[k], white, i, x, mask, c.side_pieces);
                        c.slider_gain[0][x] += plan_cut(kinds[k], white, false, lost);
                        c.slider_gain[1][x] += plan_cut(kinds[k], white, true, lost);
                    }
                }
                else
                plan_add_leaper(c.leaper_gain, kinds[k], white, i, 1);
            }
        }
    }
}

// The best target of the `TYPE` piece p (Board[] index) on `from`: a BFS by
// level, with db at every square reached. A reached square's own attacks are
// both the next level's step and the piece's activity there. `scratch` is the
// position to re-evaluate in reference mode, nullptr otherwise. A square in
// `excl` is passed through but is no target (Q2: taken by another piece).
// With Q2, ties in term go to the lower `key` = the target in the owner's view,
// so the choice is the same in the mirrored position.
// KEEP: what else the piece keeps in cand[] - PLAN_KEEP_LIST (Q2 list): its
// PLAN_CANDIDATES best targets, best first by (term, key); PLAN_KEEP_TWO (Q2
// on conflicts): the runner-up after the best, if any.
struct Plan_Cand { int term, key, to, n, db; };
constexpr int PLAN_KEEP_BEST = 0, PLAN_KEEP_LIST = 1, PLAN_KEEP_TWO = 2;

static inline bool plan_cand_before(int term, int key, const Plan_Cand& b)
{
    return term > b.term || (term == b.term && key < b.key);
}

template<int TYPE, int KEEP>
static Plan_Target plan_piece(Plan_Ctx& c, int p, int from, BB* scratch, Plan_Cand* cand, int* n_cand, uint64_t excl)
{
    constexpr bool SLIDER = TYPE == 1 || TYPE == 3 || TYPE == 4;
    constexpr bool DIAG = TYPE == 3 || TYPE == 4;
    constexpr bool ORTH = TYPE == 1 || TYPE == 4;
    constexpr int LEAPER = TYPE == 0 ? ROLE_PAWN : TYPE == 2 ? ROLE_KNIGHT : ROLE_KING;
    constexpr bool OWN = PLAN_OWN_ACT;
    const WEIGHTS& W = *c.W;
    const bool white = p < 6;
    const int sign = white ? 1 : -1;
    const int row = white ? p : (p==6 ? 6 : p-6);
    const uint64_t from_bb = 1ULL << from;
    const uint64_t others = c.occupancy & ~from_bb;
    const uint64_t allowed = ~c.occupancy & ~c.avoid[white][TYPE];

    // what does not depend on the target: the board with `from` empty
    const int ow = c.OW[white], ew = c.EW[white];
    const int pt_base = c.pt39 - sign*(W.piece_table_value_opening[row][from]*ow + W.piece_table_value_endgame[row][from]*ew);
    const int pt_now = c.pt39/39, act_now = c.act/2;
    const uint64_t* pawn_from = white ? WP_template : BP_template;// [x]: the squares an own pawn attacks x from
    const uint64_t* pawn_to = white ? BP_template : WP_template;// [x]: the squares an own pawn on x attacks
    const uint64_t other_pawns = c.original->Board[6*!white] & ~from_bb;
    int ps_from = count(other_pawns & pawn_from[from]);
    if(TYPE == 0)
    ps_from += count(c.side_pieces[white] & pawn_to[from]);
    uint64_t sp[2] = {c.side_pieces[0], c.side_pieces[1]};
    sp[white] &= ~from_bb;
    const uint64_t own = sp[white], enemy = sp[!white];
    // leaper_gain[] also holds the piece's own leaper role, which moves along:
    // its mask never holds `from`, and holds a target only for a knight or king
    // one move away (an own piece there) or the square in front of a pawn
    const uint64_t own_mask = OWN ? 0 : TYPE == 0 ? (white ? from_bb << 8 : from_bb >> 8) : TYPE == 2 ? Kn_template[from] : TYPE == 5 ? K_template[from] : 0;
    const int own_w = TYPE == 0 ? PLAN_PAWN_BLOCKED : PLAN_ROLE_W[LEAPER][0];
    const int own_gain = white ? own_w : -own_w;
    int vacate = -c.term_at[from];
    // slider_gain[] is right for every target except through the piece's own
    // sliders and those that see `from` (they see more with it empty)
    int own_r[2] = {0,0};
    uint64_t vac_sl = 0, vac_seen = 0;
    if(!OWN)
    {
        vacate -= c.leaper_gain[white][from];
        const uint64_t own_sl = c.sl_on[from];
        for(uint64_t v = own_sl, k = 0; v; k++)
        own_r[k] = find_and_delete_trailling_1(v);
        vac_sl = c.sl_seeing[from] & ~own_sl;
        for(uint64_t v = vac_sl; v; )
        {
            const int r = find_and_delete_trailling_1(v);
            const bool rw = c.sl_white[r];
            const uint64_t a = c.sl_kind[r] == ROLE_DIAG ? get_bishop_attacks(c.sl_sq[r],others) : get_rook_attacks(c.sl_sq[r],others);
            vacate += plan_masked_term(c.sl_kind[r], rw, a, sp[rw], sp[!rw]) - c.sl_term[r];
            vac_seen |= a;// a superset of sl_att0[r]
            c.sl_att[r] = a;
        }
    }
    // reference mode, OWN: the piece's own terms where it stands
    const int ref_from = OWN && scratch ? plan_ref_activity(c.original, p, from) : 0;

    int b_to = -1, b_n = 0, b_db = 0, b_term = -1, b_key = 64;
    int s_to = -1, s_n = 0, s_db = 0, s_term = -1, s_key = 64;// KEEP_TWO: the runner-up
    uint64_t reached = from_bb;
    uint64_t level = plan_step(TYPE, white, from, others) & allowed & ~reached;
    int nc = 0;
    for(int n=1; level && (PLAN_N_CAP == 0 || n <= PLAN_N_CAP); n++)
    {
        reached |= level;
        const int divisor = 2 + n;
        uint64_t next = 0;
        uint64_t targets = level;
        while(targets)
        {
            const int to = find_and_delete_trailling_1(targets);
            // activity, before the /2
            int delta = vacate;
            if(!OWN)
            delta += c.leaper_gain[white][to] + c.slider_gain[white][to];
            if(!OWN && !SLIDER && own_mask >> to & 1)
            delta -= own_gain;
            if(SLIDER)
            {
                const uint64_t diag = DIAG ? get_bishop_attacks(to,others) : 0;
                const uint64_t orth = ORTH ? get_rook_attacks(to,others) : 0;
                next |= diag | orth;
                if(DIAG)
                delta += plan_masked_term(ROLE_DIAG, white, diag, own, enemy);
                if(ORTH)
                delta += plan_masked_term(ROLE_ORTH, white, orth, own, enemy);
                if(!OWN)
                for(int k = 0; k < (TYPE == 4 ? 2 : 1); k++)
                {
                    const int r = own_r[k];
                    if(c.sl_att0[r] >> to & 1)
                    delta -= plan_cut(c.sl_kind[r], white, white, c.sl_lost[r][to]);
                }
            }
            else if(TYPE == 0)
            {
                next |= plan_step(0, white, to, others);
                uint64_t unused;
                delta += plan_role_term(ROLE_PAWN, white, to, others, sp[1], sp[0], &unused);
            }
            else
            {
                const uint64_t m = TYPE == 2 ? Kn_template[to] : K_template[to];
                next |= m;
                delta += plan_masked_term(LEAPER, white, m, own, enemy);
            }
            if(!OWN && vac_seen >> to & 1)
            for(uint64_t v = vac_sl; v; )
            {
                const int r = find_and_delete_trailling_1(v);
                const bool rw = c.sl_white[r];
                if(c.sl_att0[r] >> to & 1)
                delta -= plan_cut(c.sl_kind[r], rw, white, c.sl_lost[r][to]);
                if(c.sl_att[r] >> to & 1)
                delta += plan_cut(c.sl_kind[r], rw, white, plan_lost(c.sl_kind[r], rw, c.sl_sq[r], to, c.sl_att[r], sp));
            }
            int db;
            if(scratch)
            {
                const uint64_t move = from_bb | (1ULL << to);
                scratch->Board[p] ^= move;
                if(OWN)// the others' activity frozen at the position's, the moved piece's own terms at `to`
                {
                    const int act_to = c.ref_act - ref_from + plan_ref_activity(scratch, p, to);
                    db = sign * (piecetable(scratch,W) + positional_eval(scratch,W) - c.base + act_to/2 - c.ref_act/2);
                }
                else
                db = sign * (plan_partial_eval(scratch,W) - c.base);
                scratch->Board[p] ^= move;
            }
            else
            {
                const int pt_to = W.piece_table_value_opening[row][to]*ow + W.piece_table_value_endgame[row][to]*ew;
                const int d = (pt_base + sign*pt_to)/39 - pt_now + (c.act + delta)/2 - act_now;
                // pawn structure, the owner's own view
                int ps_to = count(other_pawns & pawn_from[to]);
                if(TYPE == 0)
                ps_to += count(own & pawn_to[to]);
                db = sign*d + W.pawn_supporting_value*(ps_to - ps_from);
            }
            // with the net (#67): what its correction gains, owner's view, on
            // the board with just this piece moved (side to move unchanged)
            if(c.nne_board)
            {
                const uint64_t move = from_bb | (1ULL << to);
                c.nne_board->Board[p] ^= move;
                db += (int)std::lround(sign * (plan_nne_white(*c.nne_board) - c.nne_base));
                c.nne_board->Board[p] ^= move;
            }
            // the highest db/divisor, if its db > 0 (Q2: then the lowest key,
            // otherwise the first found); branch-free
            const int term = db / divisor;
            const int key = white ? to : to ^ 56;
            // (the list's greedy ignores the best, so it is not tracked there)
            const bool ok = (KEEP != PLAN_KEEP_LIST) & (db > 0) & !(KEEP == PLAN_KEEP_TWO && excl >> to & 1);
            const bool better = ok & ((term > b_term) | (PLAN_Q2_ONE_PER_SQUARE & (term == b_term) & (key < b_key)));
            if(KEEP == PLAN_KEEP_TWO)
            {
                // the old best moves down, or the target goes between
                const bool second = ok & !better & ((term > s_term) | ((term == s_term) & (key < s_key)));
                s_to = better ? b_to : second ? to : s_to;
                s_n = better ? b_n : second ? n : s_n;
                s_db = better ? b_db : second ? db : s_db;
                s_term = better ? b_term : second ? term : s_term;
                s_key = better ? b_key : second ? key : s_key;
            }
            b_to = better ? to : b_to;
            b_n = better ? n : b_n;
            b_db = better ? db : b_db;
            b_term = better ? term : b_term;
            b_key = better ? key : b_key;
            if(KEEP == PLAN_KEEP_LIST && db > 0)
            {
                int k = nc;
                if(nc < PLAN_CANDIDATES)
                nc++;
                else if(plan_cand_before(term, key, cand[PLAN_CANDIDATES-1]))
                k = PLAN_CANDIDATES-1;
                else
                continue;
                for(; k > 0 && plan_cand_before(term, key, cand[k-1]); k--)
                cand[k] = cand[k-1];
                cand[k] = {term, key, to, n, db};
            }
        }
        level = next & allowed & ~reached;
    }
    for(uint64_t v = vac_sl; v; )
    {
        const int r = find_and_delete_trailling_1(v);
        c.sl_att[r] = c.sl_att0[r];
    }
    if(KEEP == PLAN_KEEP_LIST)
    *n_cand = nc;
    if(KEEP == PLAN_KEEP_TWO)
    {
        cand[0] = {s_term, s_key, s_to, s_n, s_db};
        *n_cand = s_to >= 0;
    }
    return {p, from, b_to, b_n, b_db, b_to == -1 ? 0 : b_term};
}

template<int KEEP>
static Plan_Target plan_one(Plan_Ctx& c, int p, int from, BB* scratch, Plan_Cand* pc, int* nc, uint64_t excl)
{
    switch(p % 6)
    {
        case 0: return plan_piece<0,KEEP>(c, p, from, scratch, pc, nc, excl);
        case 1: return plan_piece<1,KEEP>(c, p, from, scratch, pc, nc, excl);
        case 2: return plan_piece<2,KEEP>(c, p, from, scratch, pc, nc, excl);
        case 3: return plan_piece<3,KEEP>(c, p, from, scratch, pc, nc, excl);
        case 4: return plan_piece<4,KEEP>(c, p, from, scratch, pc, nc, excl);
        default:
        if(PLAN_Q3_KING_HOME && c.original->Board[p < 6 ? 10 : 4])
        {
            if(KEEP != PLAN_KEEP_BEST)
            *nc = 0;
            return {p, from, -1, 0, 0, 0};
        }
        return plan_piece<5,KEEP>(c, p, from, scratch, pc, nc, excl);
    }
}

// Q2 conflicts, for the probe: evaluations, those with a clash, clashes the
// runner-up settled, and searches repeated because of one.
static uint64_t plan_q2_evals = 0, plan_q2_clash_evals = 0, plan_q2_runner_ups = 0, plan_q2_reruns = 0;

static int plan_all_pieces(Plan_Ctx& c, BB* scratch, Plan_Target* out, int* count_out, int q2)
{
    int score = 0;
    int n_out = 0;
    Plan_Target all[32];
    Plan_Cand cand[32][PLAN_CANDIDATES];
    int n_cand[32];
    for(int p=0;p<12;p++)
    {
        uint64_t pieces = c.original->Board[p];
        while(pieces)
        {
            const int from = find_and_delete_trailling_1(pieces);
            all[n_out] = q2 == PLAN_Q2_LIST ? plan_one<PLAN_KEEP_LIST>(c, p, from, scratch, cand[n_out], &n_cand[n_out], 0)
                       : q2 == PLAN_Q2_CONFLICTS ? plan_one<PLAN_KEEP_TWO>(c, p, from, scratch, cand[n_out], &n_cand[n_out], 0)
                       : plan_one<PLAN_KEEP_BEST>(c, p, from, scratch, nullptr, nullptr, 0);
            n_out++;
        }
    }
    if(q2 == PLAN_Q2_LIST)
    {
        // Per side, greedily: of the pieces without a target, the one whose
        // best square not yet taken has the highest term takes it. Ties go by
        // the squares in the owner's view, so the mirrored position agrees.
        for(int side=0;side<2;side++)
        {
            uint64_t taken = 0;
            uint32_t open = 0;
            int head[32];
            for(int k=0;k<n_out;k++)
            if((all[k].piece < 6) == side)
            {
                open |= 1u << k;
                head[k] = 0;
                all[k].to = -1;
                all[k].n = all[k].db = all[k].term = 0;
            }
            while(open)
            {
                int bk = -1;
                for(uint32_t o = open; o; o &= o-1)
                {
                    const int k = __builtin_ctz(o);
                    while(head[k] < n_cand[k] && taken >> cand[k][head[k]].to & 1)
                    head[k]++;
                    if(head[k] == n_cand[k])
                    {
                        open &= ~(1u << k);
                        continue;
                    }
                    const Plan_Cand& h = cand[k][head[k]];
                    const int from_key = side ? all[k].from : all[k].from ^ 56;
                    if(bk < 0)
                    {
                        bk = k;
                        continue;
                    }
                    const Plan_Cand& b = cand[bk][head[bk]];
                    const int b_from_key = side ? all[bk].from : all[bk].from ^ 56;
                    if(plan_cand_before(h.term, h.key, b) || (h.term == b.term && h.key == b.key && from_key < b_from_key))
                    bk = k;
                }
                if(bk < 0)
                break;
                const Plan_Cand& h = cand[bk][head[bk]];
                taken |= 1ULL << h.to;
                open &= ~(1u << bk);
                all[bk].to = h.to;
                all[bk].n = h.n;
                all[bk].db = h.db;
                all[bk].term = h.term;
            }
        }
    }
    else if(q2 == PLAN_Q2_CONFLICTS)
    {
        // The same greedy on each piece's best target only: a piece whose
        // best square was taken takes its runner-up when that is free, and
        // otherwise searches again without the taken squares.
        plan_q2_evals++;
        const uint64_t clashes_before = plan_q2_reruns + plan_q2_runner_ups;
        for(int side=0;side<2;side++)
        {
            uint64_t taken = 0;
            uint32_t open = 0;
            for(int k=0;k<n_out;k++)
            if((all[k].piece < 6) == side && all[k].to >= 0)
            open |= 1u << k;
            while(open)
            {
                int bk = -1, b_key = 0, b_from_key = 0;
                for(uint32_t o = open; o; o &= o-1)
                {
                    const int k = __builtin_ctz(o);
                    if(taken >> all[k].to & 1)
                    {
                        const Plan_Cand& r = cand[k][0];
                        if(n_cand[k] && !(taken >> r.to & 1))// the runner-up is free: it is the best left
                        {
                            plan_q2_runner_ups++;
                            all[k].to = r.to;
                            all[k].n = r.n;
                            all[k].db = r.db;
                            all[k].term = r.term;
                            n_cand[k] = 0;// the next one is not known
                        }
                        else
                        {
                            plan_q2_reruns++;
                            all[k] = plan_one<PLAN_KEEP_TWO>(c, all[k].piece, all[k].from, scratch, cand[k], &n_cand[k], taken);
                            if(all[k].to < 0)
                            {
                                open &= ~(1u << k);
                                continue;
                            }
                        }
                    }
                    const int key = side ? all[k].to : all[k].to ^ 56;
                    const int from_key = side ? all[k].from : all[k].from ^ 56;
                    if(bk < 0 || all[k].term > all[bk].term
                       || (all[k].term == all[bk].term && (key < b_key || (key == b_key && from_key < b_from_key))))
                    {
                        bk = k;
                        b_key = key;
                        b_from_key = from_key;
                    }
                }
                if(bk < 0)
                break;
                taken |= 1ULL << all[bk].to;
                open &= ~(1u << bk);
            }
        }
        plan_q2_clash_evals += plan_q2_reruns + plan_q2_runner_ups != clashes_before;
    }
    for(int k=0;k<n_out;k++)
    {
        score += all[k].piece < 6 ? all[k].term : -all[k].term;
        if(out)
        out[k] = all[k];
    }
    if(count_out)
    *count_out = n_out;
    return score;
}

static int plan_eval_impl(const BB* const original, const WEIGHTS& W, Plan_Target* out, int* count_out, bool reference, int q2, bool with_nne = false)
{
    Plan_Ctx c;
    plan_setup(c, original, W);
    BB nne_board = *original;
    c.nne_board = with_nne ? &nne_board : nullptr;
    c.nne_base = with_nne ? plan_nne_white(nne_board) : 0;
    const int tempo = original->white_move ? PLAN_TEMPO : -PLAN_TEMPO;
    int sum;
    if(!reference)
    sum = plan_all_pieces(c, nullptr, out, count_out, q2);
    else
    {
        BB scratch = *original;// only Board[] is read by the partial evals, so the lazy cache is left alone
        if(PLAN_OWN_ACT)
        {
            c.ref_act = plan_ref_activity_all(&scratch);
            c.base = piecetable(&scratch,W) + positional_eval(&scratch,W);
        }
        else
        c.base = plan_partial_eval(&scratch,W);
        sum = plan_all_pieces(c, &scratch, out, count_out, q2);
    }
    const int term = PLAN_SCALE == 100 ? tempo + sum : (tempo + sum) * PLAN_SCALE / 100;
    return term > PLAN_BOUND ? PLAN_BOUND : term < -PLAN_BOUND ? -PLAN_BOUND : term;
}

int plan_eval_detail(const BB* const original, const WEIGHTS& W, Plan_Target* out, int* count, bool reference, int q2_mode, bool with_nne)
{
    return plan_eval_impl(original,W,out,count,reference,q2_mode,with_nne);
}

// The BFS of plan_eval_impl() for one piece, kept per level: level[k] holds
// the squares first reached after k moves. Returns the number of levels filled.
static int plan_levels(const BB* const original, int piece, int from, uint64_t* level, int max_levels)
{
    const uint64_t* Board = original->Board;
    const bool white = piece < 6;
    const int type = piece % 6;
    uint64_t occupancy = 0, avoid[6];
    for(int p=0;p<12;p++)
    occupancy |= Board[p];
    plan_avoid(Board, occupancy, white, avoid);
    const uint64_t others = occupancy & ~(1ULL << from);
    const uint64_t allowed = ~occupancy & ~avoid[type];
    uint64_t reached = 1ULL << from;
    level[0] = reached;
    int n = 1;
    for(; n < max_levels && level[n-1]; n++)
    {
        uint64_t next = 0, frontier = level[n-1];
        while(frontier)
        next |= plan_step(type, white, find_and_delete_trailling_1(frontier), others);
        level[n] = next & allowed & ~reached;
        reached |= level[n];
    }
    return n;
}

int plan_path(const BB* const original, const Plan_Target& t, int* squares, uint64_t* via)
{
    *via = 0;
    if(t.to < 0)
    return 0;
    uint64_t level[64];
    const int levels = plan_levels(original, t.piece, t.from, level, 64);
    if(t.n >= levels || !(level[t.n] >> t.to & 1))
    return 0;
    const bool white = t.piece < 6;
    const int type = t.piece % 6;
    uint64_t others = 0;
    for(int p=0;p<12;p++)
    others |= original->Board[p];
    others &= ~(1ULL << t.from);
    // Backwards from the target: the squares on a shortest path at step k are
    // those of level k with a move onto one on a shortest path at step k+1.
    uint64_t on_path = 1ULL << t.to;
    *via = on_path;
    squares[t.n] = t.to;
    for(int k = t.n - 1; k >= 0; k--)
    {
        uint64_t prev = 0, candidates = level[k];
        while(candidates)
        {
            const int s = find_and_delete_trailling_1(candidates);
            if(plan_step(type, white, s, others) & on_path)
            prev |= 1ULL << s;
        }
        on_path = prev;
        *via |= prev;
        // the drawn path: the first of them with a move onto the drawn step k+1
        uint64_t drawn = prev;
        while(drawn)
        {
            const int s = find_and_delete_trailling_1(drawn);
            if(plan_step(type, white, s, others) >> squares[k+1] & 1)
            {
                squares[k] = s;
                break;
            }
        }
    }
    return t.n + 1;
}

int plan_eval(const BB* const original, const WEIGHTS& W)
{
    return plan_eval_impl(original,W,nullptr,nullptr,PLAN_REFERENCE_DB_DEFAULT,PLAN_Q2_MODE);
}

#endif // PLAN_EVAL_CPP
