// OWNERSHIP=Claude
#ifndef SEE_CPP
#define SEE_CPP
#include "../lib/see.hpp"

AttackersOfSquare attackers_of(int square, uint64_t occupancy, const uint64_t Board[12])
{
    uint64_t white_rq = (Board[1] | Board[4]) & occupancy;
    uint64_t white_bq = (Board[3] | Board[4]) & occupancy;
    uint64_t black_rq = (Board[7] | Board[10]) & occupancy;
    uint64_t black_bq = (Board[9] | Board[10]) & occupancy;

    AttackersOfSquare r;
    r.white = (WP_template[square] & Board[0] & occupancy)
            | (Kn_template[square] & Board[2] & occupancy)
            | (get_bishop_attacks(square, occupancy) & white_bq)
            | (get_rook_attacks(square, occupancy) & white_rq)
            | (K_template[square] & Board[5] & occupancy);
    r.black = (BP_template[square] & Board[6] & occupancy)
            | (Kn_template[square] & Board[8] & occupancy)
            | (get_bishop_attacks(square, occupancy) & black_bq)
            | (get_rook_attacks(square, occupancy) & black_rq)
            | (K_template[square] & Board[11] & occupancy);
    return r;
}

// Least valuable attacker among piece types 0..4 (P,R,N,B,Q) of `side_attackers`.
// The king (index 5) is left to the caller: it may only recapture last.
static bool find_least_valuable_attacker(uint64_t side_attackers, const uint64_t Board[12],
                                          int side_offset, int* out_square, int* out_piece_type)
{
    int best_piece = -1, best_value = INT_MAX, best_square = -1;
    for(int p = 0; p < 5; p++)
    {
        uint64_t candidates = side_attackers & Board[p + side_offset];
        if(candidates && SEE_PIECE_VALUE[p] < best_value)
        {
            best_value = SEE_PIECE_VALUE[p];
            best_piece = p;
            best_square = get_ls1b_index(candidates);
        }
    }
    if(best_piece == -1) return false;
    *out_square = best_square;
    *out_piece_type = best_piece;
    return true;
}

bool is_capturing_move(const uint64_t Board[12], int to_square, bool white_to_move, bool is_en_passant)
{
    if(is_en_passant) return true;
    uint64_t enemy_occ = white_to_move
        ? (Board[6] | Board[7] | Board[8] | Board[9] | Board[10] | Board[11])
        : (Board[0] | Board[1] | Board[2] | Board[3] | Board[4] | Board[5]);
    return (enemy_occ & (1ULL << to_square)) != 0;
}

int static_exchange_eval(const uint64_t Board[12], int from_square, int to_square,
                          bool white_to_move, bool is_en_passant, int promotion_piece_type)
{
    int own_offset = white_to_move ? 0 : 6;
    int enemy_offset = white_to_move ? 6 : 0;

    int moving_piece = -1;
    for(int p = 0; p < 6; p++)
        if(Board[p + own_offset] & (1ULL << from_square)) { moving_piece = p; break; }
    if(moving_piece == -1) return 0; // no own piece on from_square

    int captured_square = to_square;
    if(is_en_passant)
        captured_square = white_to_move ? (to_square - 8) : (to_square + 8);

    int captured_piece = -1;
    for(int p = 0; p < 6; p++)
        if(Board[p + enemy_offset] & (1ULL << captured_square)) { captured_piece = p; break; }

    // Only pawns ever attack a last-rank square from which they'd promote, and
    // to_square is the same square for everyone in the exchange.
    const bool last_rank = to_square < 8 || to_square >= 56;
    const int promotion_gain = SEE_PIECE_VALUE[QUEEN_PROMOTION] - SEE_PIECE_VALUE[0];

    int gain[32];
    int d = 0;
    int on_square_value = SEE_PIECE_VALUE[moving_piece];
    gain[0] = captured_piece == -1 ? 0 : SEE_PIECE_VALUE[captured_piece];
    if(moving_piece == 0 && last_rank)
    {
        int promoted = promotion_piece_type > 0 ? promotion_piece_type : QUEEN_PROMOTION;
        on_square_value = SEE_PIECE_VALUE[promoted];
        gain[0] += on_square_value - SEE_PIECE_VALUE[0];
    }

    uint64_t occ = Board[0] | Board[1] | Board[2] | Board[3] | Board[4] | Board[5]
                 | Board[6] | Board[7] | Board[8] | Board[9] | Board[10] | Board[11];
    occ &= ~(1ULL << from_square);
    if(is_en_passant) occ &= ~(1ULL << captured_square);

    bool side_to_recapture_white = !white_to_move;
    while(d + 1 < 32)
    {
        AttackersOfSquare att = attackers_of(to_square, occ, Board);
        uint64_t side_attackers = side_to_recapture_white ? att.white : att.black;
        int side_offset = side_to_recapture_white ? 0 : 6;

        int lva_square, lva_piece;
        if(!find_least_valuable_attacker(side_attackers, Board, side_offset, &lva_square, &lva_piece))
        {
            // Only the king is left: it may take if the square is then not attacked,
            // counting x-rays through the square it leaves. Nothing can follow it.
            uint64_t king = side_attackers & Board[side_offset + 5];
            if(!king) break;
            AttackersOfSquare after = attackers_of(to_square, occ & ~king, Board);
            if(side_to_recapture_white ? after.black : after.white) break;
            d++;
            gain[d] = on_square_value - gain[d - 1];
            break;
        }

        d++;
        gain[d] = on_square_value - gain[d - 1];
        occ &= ~(1ULL << lva_square);
        on_square_value = SEE_PIECE_VALUE[lva_piece];
        if(lva_piece == 0 && last_rank)
        {
            gain[d] += promotion_gain;
            on_square_value = SEE_PIECE_VALUE[QUEEN_PROMOTION];
        }
        side_to_recapture_white = !side_to_recapture_white;
    }

    while(d > 0)
    {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        d--;
    }
    return gain[0];
}

bool is_good_capture(const BB* const original, int from_square, int to_square,
                      bool is_en_passant, const WEIGHTS& /*W*/)
{
    if(!is_capturing_move(original->Board, to_square, original->white_move, is_en_passant)) return false;
    // Taking a piece worth at least the mover can't lose material even if it
    // gets taken back - no swap-off needed.
    const int own_offset = original->white_move ? 0 : 6, enemy_offset = original->white_move ? 6 : 0;
    int moving_value = 0, captured_value = SEE_PIECE_VALUE[0]; // en passant: the victim isn't on to_square
    for(int p = 0; p < 6; p++)
    {
        if(original->Board[p + own_offset] & (1ULL << from_square)) moving_value = SEE_PIECE_VALUE[p];
        if(original->Board[p + enemy_offset] & (1ULL << to_square)) captured_value = SEE_PIECE_VALUE[p];
    }
    if(captured_value >= moving_value) return true;
    return static_exchange_eval(original->Board, from_square, to_square,
                                 original->white_move, is_en_passant) >= 0;
}

bool is_good_capture_from_child(const BB* const parent, const BB* const child, const WEIGHTS& /*W*/)
{
    const int own_offset = 6 * !parent->white_move;
    uint64_t own_before = 0, own_after = 0;
    for(int p = 0; p < 6; p++)
    {
        own_before |= parent->Board[p + own_offset];
        own_after |= child->Board[p + own_offset];
    }
    uint64_t left = own_before & ~own_after;
    uint64_t arrived = own_after & ~own_before;
    if(count(left) != 1 || count(arrived) != 1) return false; // castling or no-op: not SEE-relevant here

    int from_square = get_ls1b_index(left);
    int to_square = get_ls1b_index(arrived);

    int moving_piece = -1;
    for(int p = 0; p < 6; p++)
        if(parent->Board[p + own_offset] & (1ULL << from_square)) { moving_piece = p; break; }

    uint64_t occ_before = parent->Board[0] | parent->Board[1] | parent->Board[2] | parent->Board[3]
                        | parent->Board[4] | parent->Board[5] | parent->Board[6] | parent->Board[7]
                        | parent->Board[8] | parent->Board[9] | parent->Board[10] | parent->Board[11];
    bool is_en_passant = (moving_piece == 0)
                       && ((from_square % 8) != (to_square % 8))
                       && !(occ_before & (1ULL << to_square));

    if(!is_capturing_move(parent->Board, to_square, parent->white_move, is_en_passant)) return false;
    return static_exchange_eval(parent->Board, from_square, to_square,
                                 parent->white_move, is_en_passant) >= 0;
}

#endif // SEE_CPP
