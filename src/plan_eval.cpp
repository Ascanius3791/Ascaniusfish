// OWNERSHIP=Claude
#ifndef PLAN_EVAL_CPP
#define PLAN_EVAL_CPP
#include "../lib/plan_eval.hpp"

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

// ---- db without re-evaluating the board ------------------------------------
// The gain of moving one piece s -> t is the difference of the three partial
// evals, each computed exactly (the same integers, the same truncations):
// - piecetable() is a sum in 39ths divided once, so only the piece's two
//   table entries change the sum;
// - pawn_struckture_eval_of_colour(): a push keeps the pawn's file, so only
//   the "pawn supports a piece" counts at s and t change;
// - piece_activity_eval() is a sum of one term per piece role (a queen has a
//   diagonal and an orthogonal one) divided by 2 at the end. A role's term
//   reads the occupancy only inside its mask (a slider's attacks, a leaper's
//   template, a pawn's attack/front/side squares), so only the moved piece's
//   roles and those whose mask holds s or t change. For a slider that is
//   exact: if neither s nor t is in its attacks, neither square is seen and
//   neither blocks a ray it sees.
// PLAN_REFERENCE_DB recomputes the partial evals instead (the definition);
// diagnostics/plan_eval_probe.cpp checks the two agree everywhere.

enum Plan_Role_Kind { ROLE_PAWN, ROLE_DIAG, ROLE_ORTH, ROLE_KNIGHT, ROLE_KING };

struct Plan_Role
{
    int kind, sq;
    bool white;
    uint64_t mask;
    int term;// white's view, before piece_activity_eval()'s /2
};

// One role's piece_activity_eval() term, white's view; *mask gets what it reads.
static inline int plan_role_term(int kind, bool white, int i, uint64_t occ, uint64_t white_pieces, uint64_t black_pieces, uint64_t* mask)
{
    const int sg = white ? 1 : -1;
    const uint64_t own = white ? white_pieces : black_pieces;
    const uint64_t enemy = white ? black_pieces : white_pieces;
    switch(kind)
    {
        case ROLE_PAWN:
        {
            const uint64_t att = white ? BP_template[i] : WP_template[i];
            const uint64_t front = white ? 1ULL << i+8 : 1ULL << i >> 8;
            const uint64_t side = white ? (BP_template[i]<<8 | (i/8==1 ? BP_template[i]<<16 : 0))
                                        : (WP_template[i]>>8 | (i/8==6 ? WP_template[i]>>16 : 0));
            *mask = att | front | side;
            int t = count(enemy & att)*30 + count(own & att)*15 + count(enemy & side)*20 + count(own & side)*10;
            if(occ & front)
            t -= 20;
            return sg*t;
        }
        case ROLE_DIAG:
        {
            const uint64_t a = get_bishop_attacks(i,occ);
            *mask = a;
            return sg*(count(own & a)*10 + count(enemy & a)*40 + count(a)*5);
        }
        case ROLE_ORTH:
        {
            const uint64_t a = get_rook_attacks(i,occ);
            *mask = a;
            return sg*(-count(own & a)*10 + count(enemy & a)*40 + count(a)*7);
        }
        case ROLE_KNIGHT:
            *mask = Kn_template[i];
            return sg*(-count(own & Kn_template[i])*10 + count(enemy & Kn_template[i])*10 + count(Kn_template[i])*5);
        default:
            *mask = K_template[i];
            return sg*(count(own & K_template[i])*15 + count(enemy & K_template[i])*20);
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

constexpr bool PLAN_REFERENCE_DB_DEFAULT = false;

static int plan_eval_impl(const BB* const original, const WEIGHTS& W, Plan_Target* out, int* count_out, bool reference)
{
    const uint64_t* Board = original->Board;
    uint64_t side_pieces[2] = {0,0};// [1] = white
    for(int p=0;p<6;p++)
    {
        side_pieces[1] |= Board[p];
        side_pieces[0] |= Board[p+6];
    }
    const uint64_t occupancy = side_pieces[0] | side_pieces[1];
    uint64_t pawn_attacks[2] = {0,0};// [1] = attacked by white pawns
    for(int col=0;col<2;col++)
    {
        uint64_t pawns = Board[6*!col];
        while(pawns)
        {
            int i=find_and_delete_trailling_1(pawns);
            pawn_attacks[col] |= col ? BP_template[i] : WP_template[i];
        }
    }
    const uint64_t last_ranks = mask_row[0] | mask_row[7];

    // piecetable() in 39ths, and its phase weights
    int OW[2], EW[2];// [1] = white's: the enemy (black) material left
    for(int col=0;col<2;col++)
    {
        const int e = 6*col;// col 1 (white) counts black's Board[6..]
        OW[col] = count(Board[0+e]) + 5*count(Board[1+e]) + 3*count(Board[2+e]) + 3*count(Board[3+e]) + 9*count(Board[4+e]);
        EW[col] = 39 - OW[col];
    }
    int pt39 = 0;
    for(int p=0;p<12;p++)
    {
        const bool white = p < 6;
        const int row = white ? p : (p==6 ? 6 : p-6);
        uint64_t bb = Board[p];
        while(bb)
        {
            const int i = find_and_delete_trailling_1(bb);
            const int v = W.piece_table_value_opening[row][i]*OW[white] + W.piece_table_value_endgame[row][i]*EW[white];
            pt39 += white ? v : -v;
        }
    }

    // piece_activity_eval() by role
    Plan_Role roles[48];
    int n_roles = 0;
    int act = 0;
    for(int p=0;p<12;p++)
    {
        int kinds[2];
        const int nk = plan_roles_of(p % 6, kinds);
        uint64_t bb = Board[p];
        while(bb)
        {
            const int i = find_and_delete_trailling_1(bb);
            for(int k=0;k<nk;k++)
            {
                Plan_Role& r = roles[n_roles++];
                r.kind = kinds[k];
                r.sq = i;
                r.white = p < 6;
                r.term = plan_role_term(r.kind, r.white, i, occupancy, side_pieces[1], side_pieces[0], &r.mask);
                act += r.term;
            }
        }
    }
    uint64_t roles_seeing[64] = {};// bit r: roles[r].mask holds the square
    for(int r=0;r<n_roles;r++)
    {
        uint64_t m = roles[r].mask;
        while(m)
        roles_seeing[find_and_delete_trailling_1(m)] |= 1ULL << r;
    }

    BB scratch = *original;// reference mode: only Board[] is read by the partial evals, so the lazy cache is left alone
    const int base = reference ? plan_partial_eval(&scratch,W) : 0;
    int score = 0;
    int n_out = 0;
    for(int p=0;p<12;p++)
    {
        const bool white = p < 6;
        const int type = p % 6;
        const int sign = white ? 1 : -1;
        const int row = white ? p : (p==6 ? 6 : p-6);
        const int tempo = white == original->white_move ? 1 : 0;
        int kinds[2];
        const int nk = plan_roles_of(type, kinds);
        const uint64_t own_pawns = Board[6*!white];
        uint64_t pieces = Board[p];
        while(pieces)
        {
            const int from = find_and_delete_trailling_1(pieces);
            const uint64_t from_bb = 1ULL << from;
            const uint64_t others = occupancy & ~from_bb;
            uint64_t allowed = ~occupancy;
            if(type == 0)
            allowed &= ~last_ranks;
            else
            allowed &= ~pawn_attacks[!white];

            // what does not depend on the target
            int own_roles_term = 0;
            uint64_t own_roles = 0;
            for(int r=0;r<n_roles;r++)
            if(roles[r].sq == from)
            {
                own_roles_term += roles[r].term;
                own_roles |= 1ULL << r;
            }
            const int pt_from = W.piece_table_value_opening[row][from]*OW[white] + W.piece_table_value_endgame[row][from]*EW[white];
            const uint64_t other_pawns = own_pawns & ~from_bb;
            const uint64_t attackers_from = white ? WP_template[from] : BP_template[from];// squares an own pawn attacks `from` from
            int ps_from = count(other_pawns & attackers_from);
            if(type == 0)
            ps_from += count(side_pieces[white] & (white ? BP_template[from] : WP_template[from]));

            Plan_Target best = {p, from, -1, 0, 0, 0};
            uint64_t reached = from_bb;
            uint64_t frontier = reached;
            for(int n=1; frontier; n++)
            {
                uint64_t next = 0;
                while(frontier)
                next |= plan_step(type, white, find_and_delete_trailling_1(frontier), others);
                next &= allowed & ~reached;
                reached |= next;
                frontier = next;
                const int divisor = 2 + n - tempo;
                uint64_t targets = next;
                while(targets)
                {
                    const int to = find_and_delete_trailling_1(targets);
                    const uint64_t move = from_bb | (1ULL << to);
                    int db;
                    if(reference)
                    {
                        scratch.Board[p] ^= move;
                        db = sign * (plan_partial_eval(&scratch,W) - base);
                        scratch.Board[p] ^= move;
                    }
                    else
                    {
                        // piece tables
                        const int pt_to = W.piece_table_value_opening[row][to]*OW[white] + W.piece_table_value_endgame[row][to]*EW[white];
                        const int pt_after = pt39 + sign*(pt_to - pt_from);
                        int d = pt_after/39 - pt39/39;
                        // activity
                        const uint64_t occ2 = occupancy ^ move;
                        uint64_t pieces2[2] = {side_pieces[0], side_pieces[1]};
                        pieces2[white] ^= move;
                        int act_after = act - own_roles_term;
                        uint64_t unused;
                        for(int k=0;k<nk;k++)
                        act_after += plan_role_term(kinds[k], white, to, occ2, pieces2[1], pieces2[0], &unused);
                        uint64_t touched = (roles_seeing[from] | roles_seeing[to]) & ~own_roles;
                        while(touched)
                        {
                            const Plan_Role& r = roles[find_and_delete_trailling_1(touched)];
                            act_after += plan_role_term(r.kind, r.white, r.sq, occ2, pieces2[1], pieces2[0], &unused) - r.term;
                        }
                        d += act_after/2 - act/2;
                        // pawn structure, the owner's own view
                        int ps_to = count(other_pawns & (white ? WP_template[to] : BP_template[to]));
                        if(type == 0)
                        ps_to += count(pieces2[white] & (white ? BP_template[to] : WP_template[to]));
                        db = sign*d + W.pawn_supporting_value*(ps_to - ps_from);
                    }
                    if(db <= 0)
                    continue;
                    const int term = db / divisor;
                    if(best.to == -1 || term > best.term)
                    best = {p, from, to, n, db, term};
                }
            }
            score += sign * best.term;
            if(out)
            out[n_out++] = best;
        }
    }
    if(count_out)
    *count_out = n_out;
    return score;
}

int plan_eval_detail(const BB* const original, const WEIGHTS& W, Plan_Target* out, int* count, bool reference)
{
    return plan_eval_impl(original,W,out,count,reference);
}

// The BFS of plan_eval_impl() for one piece, kept per level: level[k] holds
// the squares first reached after k moves. Returns the number of levels filled.
static int plan_levels(const BB* const original, int piece, int from, uint64_t* level, int max_levels)
{
    const uint64_t* Board = original->Board;
    const bool white = piece < 6;
    const int type = piece % 6;
    uint64_t occupancy = 0, enemy_pawn_attacks = 0;
    for(int p=0;p<12;p++)
    occupancy |= Board[p];
    uint64_t pawns = Board[white ? 6 : 0];
    while(pawns)
    {
        const int i = find_and_delete_trailling_1(pawns);
        enemy_pawn_attacks |= white ? WP_template[i] : BP_template[i];// as pawn_attacks[] in plan_eval_impl()
    }
    const uint64_t others = occupancy & ~(1ULL << from);
    uint64_t allowed = ~occupancy;
    if(type == 0)
    allowed &= ~(mask_row[0] | mask_row[7]);
    else
    allowed &= ~enemy_pawn_attacks;
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
    if(!USE_PLAN_EVAL)
    return 0;
    return plan_eval_impl(original,W,nullptr,nullptr,PLAN_REFERENCE_DB_DEFAULT);
}

#endif // PLAN_EVAL_CPP
