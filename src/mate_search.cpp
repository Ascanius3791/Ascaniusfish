// OWNERSHIP=Claude
#ifndef MATE_SEARCH_CPP
#define MATE_SEARCH_CPP
#include "../lib/mate_search.hpp"
#include <algorithm>

// Plies fit in a byte of a TT entry.
constexpr int MATE_MAX_PLIES = 250;

// What mate_line() may spend on children the TT has lost.
constexpr long long MATE_LINE_BUDGET = 200000;

// 16 bytes. The three bounds are about this position with the attacker of
// the key (see mate_key()): mate_in, a mate within that many plies (any mode
// may use it: a mate found is a mate); no_full, none within that many (proven
// by FULL_WIDTH, so CHECKS_ONLY may use it too); no_checks, none the
// attacker's checks reach (CHECKS_ONLY only). 0 = not known.
struct Mate_TT_Entry
{
    uint64_t key;
    uint16_t move;       // the mating move, or the defence that held; 0 = none
    uint8_t mate_in;
    uint8_t no_full;
    uint8_t no_checks;
    uint8_t unused[3];
};
static_assert(sizeof(Mate_TT_Entry) == 16, "a mate TT entry is 16 bytes");

constexpr int MATE_TT_WAYS = 4;  // one 64-byte line per bucket

static std::vector<Mate_TT_Entry>& mate_tt_storage()
{
    static std::vector<Mate_TT_Entry> table;
    return table;
}

static std::vector<Mate_TT_Entry>& mate_tt()
{
    std::vector<Mate_TT_Entry>& table = mate_tt_storage();
    if(table.empty())
    table.assign(size_t(1) << MATE_TT_BITS, Mate_TT_Entry{});
    return table;
}

void mate_tt_clear()
{
    std::vector<Mate_TT_Entry>& t = mate_tt_storage();  // nothing to clear before the first search
    std::fill(t.begin(), t.end(), Mate_TT_Entry{});
}

// The position's Zobrist key, told apart by who is to mate.
static inline uint64_t mate_key(const BB* const pos, bool attacker_white)
{
    return pos->zobrist_hash ^ (attacker_white ? 0x9E3779B97F4A7C15ULL : 0);
}

static inline Mate_TT_Entry* mate_tt_bucket(uint64_t key)
{
    std::vector<Mate_TT_Entry>& t = mate_tt();
    return &t[(key & ((t.size()/MATE_TT_WAYS)-1)) * MATE_TT_WAYS];
}

static inline const Mate_TT_Entry* mate_tt_probe(uint64_t key)
{
    Mate_TT_Entry* b = mate_tt_bucket(key);
    for(int i=0;i<MATE_TT_WAYS;i++)
    if(b[i].key == key)
    return &b[i];
    return nullptr;
}

// Merges what a node found into its entry; a full bucket gives up the entry
// that holds the least work (the smallest bound).
static void mate_tt_store(uint64_t key, int mate_in, int no_full, int no_checks, uint16_t move)
{
    Mate_TT_Entry* b = mate_tt_bucket(key);
    Mate_TT_Entry* e = nullptr;
    int victim = 0, least = INT_MAX;
    for(int i=0;i<MATE_TT_WAYS && !e;i++)
    {
        if(b[i].key == key)
        e = &b[i];
        const int work = std::max({(int)b[i].mate_in, (int)b[i].no_full, (int)b[i].no_checks});
        if(work < least)
        {
            least = work;
            victim = i;
        }
    }
    if(!e)
    {
        e = &b[victim];
        *e = Mate_TT_Entry{};
        e->key = key;
    }
    if(mate_in > 0)
    e->mate_in = e->mate_in ? std::min((int)e->mate_in, mate_in) : mate_in;
    e->no_full = std::max((int)e->no_full, no_full);
    e->no_checks = std::max((int)e->no_checks, no_checks);
    if(move)
    e->move = move;
}

static inline uint16_t mate_move_code(const Move& m)
{
    return uint16_t(m.from | (m.to << 6) | ((m.promotion_piece_type + 1) << 12));
}

static int find_move_code(const Move_List& moves, int n, uint16_t code)
{
    if(code)
    for(int i=0;i<n;i++)
    if(mate_move_code(moves[i]) == code)
    return i;
    return -1;
}

// The squares one piece of board type `type` (0..5: P,R,N,B,Q,K) of the side
// `white` attacks from sq. A white pawn's are BP_template's, as in
// attacked_squares().
static inline uint64_t piece_attacks(int type, int sq, uint64_t occupancy, bool white)
{
    switch(type)
    {
        case 0: return white ? BP_template[sq] : WP_template[sq];
        case 1: return get_rook_attacks(sq, occupancy);
        case 2: return Kn_template[sq];
        case 3: return get_bishop_attacks(sq, occupancy);
        case 4: return get_rook_attacks(sq, occupancy) | get_bishop_attacks(sq, occupancy);
        default: return K_template[sq];
    }
}

static inline int piece_on(const uint64_t B[12], int sq, bool white)
{
    const int o = white ? 0 : 6;
    for(int t=0;t<6;t++)
    if((B[t+o] >> sq) & 1)
    return t;
    return -1;
}

// What a node needs to order its moves, and to tell a check without making
// the move. The defending king's flights are the squares next to it that do
// not hold its own piece and that the attacker does not attack with the king
// off the board (so a slider's ray goes on behind it).
struct Node_Info
{
    int ksq = -1;                // the defending king
    uint64_t occupancy = 0;      // with that king off the board
    uint64_t attacks = 0;        // the attacker's
    uint64_t flights = 0;
    uint64_t direct[6] = {};     // attacker to move: where each piece type checks from
    uint64_t discoverers = 0;    // attacker to move: its pieces alone between its slider and the king
};

static Node_Info node_info(const BB* const pos, bool attacker_white, bool with_flights = true)
{
    Node_Info info;
    const uint64_t* B = pos->Board;
    const uint64_t king = B[5 + 6*attacker_white];
    if(!king)
    return info;
    info.ksq = __builtin_ctzll(king);
    info.occupancy = pos->get_occupancy() & ~king;
    const int a = attacker_white ? 0 : 6;
    if(with_flights)
    {
        for(int t=0;t<6;t++)
        for(uint64_t bb = B[t+a]; bb; bb &= bb-1)
        info.attacks |= piece_attacks(t, __builtin_ctzll(bb), info.occupancy, attacker_white);
        info.flights = K_template[info.ksq] & ~pos->get_pieces_of_colour(!attacker_white) & ~info.attacks;
    }
    if(pos->white_move != attacker_white)
    return info;
    const uint64_t occupancy = pos->get_occupancy();
    info.direct[0] = attacker_white ? WP_template[info.ksq] : BP_template[info.ksq];  // the pawns that attack ksq stand there
    info.direct[1] = get_rook_attacks(info.ksq, occupancy);
    info.direct[2] = Kn_template[info.ksq];
    info.direct[3] = get_bishop_attacks(info.ksq, occupancy);
    info.direct[4] = info.direct[1] | info.direct[3];
    const uint64_t own = pos->get_pieces_of_colour(attacker_white);
    uint64_t snipers = (get_rook_attacks(info.ksq, 0) & (B[1+a] | B[4+a])) | (get_bishop_attacks(info.ksq, 0) & (B[3+a] | B[4+a]));
    for(; snipers; snipers &= snipers-1)
    {
        const uint64_t between = between_squares.sq[info.ksq][__builtin_ctzll(snipers)] & occupancy;
        if(between && !(between & (between-1)) && (between & own))
        info.discoverers |= between;
    }
    return info;
}

// Does the attacker's move give check? 1 or 0; -1 for castling, en passant
// and promotions, which have to be made to tell.
static inline int gives_check(const BB* const pos, const Move& m, const Node_Info& info, int type)
{
    if(m.is_castling || m.is_en_passant || m.promotion_piece_type >= 0)
    return -1;
    if((info.direct[type] >> m.to) & 1)
    return 1;
    return ((info.discoverers >> m.from) & 1) && !((line_squares.sq[info.ksq][m.from] >> m.to) & 1);
}

// The defending king's flights after the attacker's move, roughly: those the
// moved piece now covers are gone (lines it opens or leaves are not counted).
static inline int flights_after_attack(const Move& m, const Node_Info& info, int type, bool attacker_white)
{
    const uint64_t occupancy = (info.occupancy & ~(1ULL << m.from)) | (1ULL << m.to);
    return __builtin_popcountll(info.flights & ~piece_attacks(type, m.to, occupancy, attacker_white));
}

// The defending king's flights after its own move, roughly: a king move counts
// the squares around its new square, any other move keeps today's.
static inline int flights_after_defence(const BB* const pos, const Move& m, const Node_Info& info)
{
    if(m.from != info.ksq)
    return __builtin_popcountll(info.flights);
    const uint64_t own = pos->get_pieces_of_colour(pos->white_move) & ~(1ULL << m.from);
    return __builtin_popcountll(K_template[m.to] & ~own & ~info.attacks);
}

constexpr int TABLES_UNKNOWN = -2;

// What the tablebases know of `pos` for the attacker: the exact plies to mate
// (Gaviota), -1 when the attacker cannot mate (Syzygy or Gaviota: not a win;
// a cursed win counts as one, the 50-move rule being ignored), or
// TABLES_UNKNOWN.
static int tables_mate(const BB* const pos, bool attacker_white)
{
    const int pieces = __builtin_popcountll(pos->get_occupancy());
    const bool attacker_to_move = pos->white_move == attacker_white;
    if(pieces <= gaviota::max_pieces())
    {
        int result, plies;
        if(gaviota::probe_dtm(pos, result, plies))
        return result == (attacker_to_move ? 1 : -1) ? plies : -1;
    }
    if(pieces <= std::min(tb_probe_limit, syzygy::max_pieces()))
    {
        int wdl;
        if(syzygy::probe_wdl(pos, wdl))
        {
            tb_hits++;
            const bool attacker_wins = attacker_to_move ? wdl > 0 : wdl < 0;
            return attacker_wins ? TABLES_UNKNOWN : -1;
        }
    }
    return TABLES_UNKNOWN;
}

// Moves k.. of order[] are still to be searched: swaps the one with the
// smallest key to k and returns it. Most nodes end after a move or two, so
// picking beats sorting.
static inline int pick_next(int* order, const int* key, int k, int n)
{
    int best = k;
    for(int j=k+1;j<n;j++)
    if(key[order[j]] < key[order[best]])
    best = j;
    std::swap(order[k], order[best]);
    return order[k];
}

int minimax_checkmate_only(const BB* const pos, BB* const wfh, int plies, Mate_Context& ctx)
{
    if(++ctx.nodes > ctx.budget)
    throw mate_budget_exhausted();
    poll_search_abort();
    const bool attacker_to_move = pos->white_move == ctx.attacker_white;
    // A mate lands on a defender's move: an odd number of plies away from an
    // attacker node, an even one from a defender node.
    plies = std::min(plies, MATE_MAX_PLIES);
    if((plies & 1) != (int)attacker_to_move)
    plies--;
    const uint64_t key = mate_key(pos, ctx.attacker_white);
    uint16_t tt_code = 0;
    if(const Mate_TT_Entry* e = mate_tt_probe(key))
    {
        if(e->mate_in && e->mate_in <= plies)
        return e->mate_in;
        if(e->no_full >= plies || (ctx.mode == Mate_Mode::CHECKS_ONLY && e->no_checks >= plies))
        return -1;
        tt_code = e->move;
    }
    Move_List moves;
    const int n = generate_legal_moves<GEN_ALL>(pos, moves);
    if(n == 0)
    return !attacker_to_move && pos->get_in_check() ? 0 : -1;
    if(plies <= 0)
    return -1;
    const int tt_move = find_move_code(moves, n, tt_code);
    int key_of[MOVE_LIST_CAP], order[MOVE_LIST_CAP];
    int m = 0;

    if(attacker_to_move)
    {
        // Checks are told without making the move; with one ply left only a
        // check can mate, and only a check is made.
        const bool checks_only = ctx.mode == Mate_Mode::CHECKS_ONLY && !pos->get_in_check();
        const Node_Info info = node_info(pos, ctx.attacker_white, plies > 1);
        for(int i=0;i<n;i++)
        {
            const Move& mv = moves[i];
            const int type = mv.promotion_piece_type >= 0 ? mv.promotion_piece_type : piece_on(pos->Board, mv.from, ctx.attacker_white);
            int check = gives_check(pos, mv, info, type);
            if(check < 0)
            {
                make_move(pos, mv, wfh);
                check = wfh->get_in_check();
            }
            if(plies == 1)
            {
                if(!check)
                continue;
                make_move(pos, mv, wfh);
                if(!wfh->get_has_legal_move())
                {
                    mate_tt_store(key, 1, 0, 0, mate_move_code(mv));
                    return 1;
                }
                continue;
            }
            if(checks_only && !check)
            continue;
            key_of[i] = i == tt_move ? -1 : (check ? 0 : 16) + flights_after_attack(mv, info, type, ctx.attacker_white);
            order[m++] = i;
        }
        if(plies == 1)
        {
            mate_tt_store(key, 0, 1, 0, 0);  // only a move that mates now would do, in any mode
            return -1;
        }
        for(int k=0;k<m;k++)
        {
            const int i = pick_next(order, key_of, k, m);
            make_move(pos, moves[i], wfh);
            int d = tables_mate(wfh, ctx.attacker_white);
            if(d == TABLES_UNKNOWN)
            d = minimax_checkmate_only(wfh, wfh+1, plies-1, ctx);
            else if(d > plies-1)
            d = -1;
            if(d >= 0)
            {
                mate_tt_store(key, d+1, 0, 0, mate_move_code(moves[i]));
                return d+1;
            }
        }
        if(ctx.mode == Mate_Mode::FULL_WIDTH)
        mate_tt_store(key, 0, plies, 0, 0);
        else
        mate_tt_store(key, 0, 0, plies, 0);
        return -1;
    }

    // The defender: every move, the one that held last time first, then the
    // ones that leave the king the most room. One move that escapes is enough.
    const Node_Info info = node_info(pos, ctx.attacker_white);
    const uint64_t attacker_pieces = pos->get_pieces_of_colour(ctx.attacker_white);
    for(int i=0;i<n;i++)
    {
        const Move& mv = moves[i];
        const bool capture = ((attacker_pieces >> mv.to) & 1) || mv.is_en_passant;
        key_of[i] = i == tt_move ? -1 : 20 - 2*flights_after_defence(pos, mv, info) - capture;
        order[m++] = i;
    }
    int longest = -1, longest_move = -1;
    for(int k=0;k<m;k++)
    {
        const int i = pick_next(order, key_of, k, m);
        make_move(pos, moves[i], wfh);
        int d = tables_mate(wfh, ctx.attacker_white);
        if(d == TABLES_UNKNOWN)
        d = minimax_checkmate_only(wfh, wfh+1, plies-1, ctx);
        else if(d > plies-1)
        d = -1;
        if(d < 0)
        {
            if(ctx.mode == Mate_Mode::FULL_WIDTH)
            mate_tt_store(key, 0, plies, 0, mate_move_code(moves[i]));
            else
            mate_tt_store(key, 0, 0, plies, mate_move_code(moves[i]));
            return -1;
        }
        if(d+1 > longest)
        {
            longest = d+1;
            longest_move = i;
        }
    }
    mate_tt_store(key, longest, 0, 0, mate_move_code(moves[longest_move]));
    return longest;
}

Mate_Result find_mate(const BB& pos, BB* const wfh, bool attacker_white, int max_plies, Mate_Mode mode,
                      long long node_budget, int full_proven)
{
    Mate_Result res;
    Mate_Context ctx;
    ctx.attacker_white = attacker_white;
    ctx.mode = mode;
    ctx.budget = node_budget;
    max_plies = std::min(max_plies, MATE_MAX_PLIES);
    int r = pos.white_move == attacker_white ? 1 : 0;  // 0: the defender may be mated already
    if(mode == Mate_Mode::FULL_WIDTH)
    while(r <= full_proven)
    r += 2;
    res.plies = full_proven;
    try
    {
        for(; r <= max_plies; r += 2)
        {
            // None within r-2 is ruled out (in this mode), so a hit is exactly r long.
            const int d = minimax_checkmate_only(&pos, wfh, r, ctx);
            if(d >= 0)
            {
                res.status = Mate_Result::FOUND;
                res.plies = d;
                res.quickest = mode == Mate_Mode::FULL_WIDTH;
                break;
            }
            if(mode == Mate_Mode::FULL_WIDTH)
            res.plies = r;
        }
        if(res.status != Mate_Result::FOUND)
        res.status = Mate_Result::NONE;
    }
    catch(const mate_budget_exhausted&)
    {
        res.status = Mate_Result::UNKNOWN;
    }
    res.nodes = ctx.nodes;
    if(res.status == Mate_Result::FOUND)
    res.line = mate_line(pos, wfh, attacker_white, res.plies, mode);
    return res;
}

bool mate_confirmed(const BB* const pos, BB* const wfh, int& eval, long long budget)
{
    const bool white_mates = eval >= INT_MAX - max_mating_seq;
    if(!white_mates && eval > INT_MIN + max_mating_seq)
    return false;  // not a mate score
    if(is_tb_score(eval))
    return false;
    Mate_Context ctx;
    ctx.attacker_white = white_mates;
    ctx.mode = Mate_Mode::CHECKS_ONLY;
    ctx.budget = budget;
    mate_checks++;
    const int claim = white_mates ? INT_MAX - eval : eval - INT_MIN;
    try
    {
        // A defence the claim never saw may last longer, so the plies go up
        // from the claim; the TT keeps each failed depth cheap for the next.
        for(int plies = claim; plies <= std::min(claim + QUIESCENCE_MATE_EXTRA_PLIES, MATE_MAX_PLIES); plies += 2)
        {
            const int found = minimax_checkmate_only(pos, wfh, plies, ctx);
            if(found < 0)
            continue;
            eval = white_mates ? INT_MAX - found : INT_MIN + found;
            mate_checks_confirmed++;
            return true;
        }
    }
    catch(const mate_budget_exhausted&)
    {
    }
    return false;
}

// The plies to mate that are known of `pos` without a search: 0 when it is
// mate, the TT's bound, the Gaviota distance; -1 if none is known.
static int known_mate(const BB* const pos, bool attacker_white)
{
    if(pos->white_move != attacker_white && pos->get_in_check() && !pos->get_has_legal_move())
    return 0;
    if(const Mate_TT_Entry* e = mate_tt_probe(mate_key(pos, attacker_white)))
    if(e->mate_in)
    return e->mate_in;
    const int t = tables_mate(pos, attacker_white);
    return t >= 0 ? t : -1;
}

std::vector<Move> mate_line(const BB& pos, BB* const wfh, bool attacker_white, int plies, Mate_Mode mode)
{
    std::vector<Move> line;
    BB cur = pos;
    int left = std::min(plies, MATE_MAX_PLIES);
    // A child the TT no longer holds is searched again: the mate was just
    // proven, so most of its tree is still there.
    Mate_Context ctx;
    ctx.attacker_white = attacker_white;
    ctx.mode = mode;
    ctx.budget = MATE_LINE_BUDGET;
    auto mate_of = [&](BB* child, int within) -> int
    {
        int d = known_mate(child, attacker_white);
        if(d >= 0 && d <= within)
        return d;
        try
        {
            return minimax_checkmate_only(child, child+1, within, ctx);
        }
        catch(const mate_budget_exhausted&)
        {
            return -1;
        }
    };
    // A TT bound may be loose, and a defence picked by it could be mated
    // sooner than the line says: the defender's moves get their exact
    // distance (deepening, as find_mate() does; the TT makes it cheap).
    auto exact_mate_of = [&](BB* child, int within) -> int
    {
        for(int r = 1; r <= within; r += 2)
        {
            const int d = mate_of(child, r);
            if(d >= 0)
            return d;
        }
        return -1;
    };
    while(left > 0)
    {
        Move_List moves;
        const int n = generate_legal_moves<GEN_ALL>(&cur, moves);
        const bool attacker_to_move = cur.white_move == attacker_white;
        int pick = -1, pick_d = -1;
        if(attacker_to_move)
        {
            // the quickest mate the TT knows, else the first one a search finds
            for(int i=0;i<n;i++)
            {
                make_move(&cur, moves[i], wfh);
                const int d = known_mate(wfh, attacker_white);
                if(d >= 0 && d <= left-1 && (pick < 0 || d < pick_d))
                {
                    pick = i;
                    pick_d = d;
                }
            }
            for(int i=0;i<n && pick<0;i++)
            {
                make_move(&cur, moves[i], wfh);
                const int d = mate_of(wfh, left-1);
                if(d >= 0)
                {
                    pick = i;
                    pick_d = d;
                }
            }
        }
        else
        {
            // the defence that lasts longest
            for(int i=0;i<n;i++)
            {
                make_move(&cur, moves[i], wfh);
                const int d = exact_mate_of(wfh, left-1);
                if(d < 0)
                {
                    pick = -1;  // out of budget: the line ends here
                    break;
                }
                if(d > pick_d)
                {
                    pick = i;
                    pick_d = d;
                }
            }
        }
        if(pick < 0)
        break;
        line.push_back(moves[pick]);
        make_move(&cur, moves[pick], wfh);
        cur = *wfh;
        left = pick_d;
    }
    return line;
}

#endif // MATE_SEARCH_CPP
