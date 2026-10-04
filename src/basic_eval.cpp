// OWNERSHIP=Ascanius
#ifndef BASIC_EVAL_CPP
#define BASIC_EVAL_CPP
#include "../lib/basic_eval.hpp"
#include "../lib/king_safety.hpp"


// material of one side in 39ths: 8 pawns, 2 rooks, 4 bishops/knights, 1 queen = 8+10+12+9
static const int MATERIAL_MAX = 8*1+5*2+3*4+9*1;
static int enemy_material_left_39ths(const BB* const original, bool for_white)
{
    int score=0;
    score += 1*count(original->Board[0+6*for_white]);
    score += 5*count(original->Board[1+6*for_white]);
    score += 3*count(original->Board[2+6*for_white]);
    score += 3*count(original->Board[3+6*for_white]);
    score += 9*count(original->Board[4+6*for_white]);
    return score;
}

float enemy_material_left_percent(const BB* const original, bool for_white)//
{
    return enemy_material_left_39ths(original,for_white)/(float)MATERIAL_MAX;
}

// TODO: crude first cut for the null-move zugzwang guard - only checks piece
// *existence*, not whether any remaining piece has a legal/reasonable move.
// A more elaborate implementation (checking move legality / reasonableness
// of the remaining pieces) may be worth the time later.
bool side_to_move_lacks_non_pawn_material(const BB* const original)
{
    int offset = 6*(!original->white_move);
    int non_pawn_pieces = count(original->Board[1+offset]) + count(original->Board[2+offset])
                         + count(original->Board[3+offset]) + count(original->Board[4+offset]);
    return non_pawn_pieces == 0;
}

int game_phase(const BB* const original)
{
    const uint64_t* B = original->Board;
    const int phase = count(B[2]|B[3]|B[8]|B[9]) + 2*count(B[1]|B[7]) + 4*count(B[4]|B[10]);
    return phase < PHASE_MAX ? phase : PHASE_MAX;
}

int piecetable(const BB* const original , const WEIGHTS& W)
{
    // Exact: the sum is kept in PHASE_MAXths as an integer and divided once, so the
    // result is colour-symmetric (int division truncates toward zero).
    const int phase = game_phase(original), endgame = PHASE_MAX-phase;
    int score=0;
    for(int piece=0; piece<6; piece++)
    {
        uint64_t white = original->Board[piece], black = original->Board[piece+6];
        while(white)
        {
            const int i=find_and_delete_trailling_1(white);
            score += W.piece_table_value_opening[piece][i]*phase + W.piece_table_value_endgame[piece][i]*endgame;
        }
        while(black)
        {
            const int i=find_and_delete_trailling_1(black)^56;// black reads white's table rank-flipped (#85)
            score -= W.piece_table_value_opening[piece][i]*phase + W.piece_table_value_endgame[piece][i]*endgame;
        }
    }
    return score/PHASE_MAX;
}

int piece_activity_eval(const BB* const original, const WEIGHTS& W)
{
    int score=0;
    int mobility=0;// in PHASE_MAXths, divided once at the end
    const int phase = game_phase(original), endgame = PHASE_MAX-phase;
    uint64_t all_black_pieces= original->Board[6]|original->Board[7]|original->Board[8]|original->Board[9]|original->Board[10]|original->Board[11];
    uint64_t all_white_pieces= original->Board[0]|original->Board[1]|original->Board[2]|original->Board[3]|original->Board[4]|original->Board[5];
    uint64_t all_pieces= all_black_pieces|all_white_pieces;
    for(int col=0;col<2;col++)
    {
        uint64_t own_pieces = col ? all_white_pieces : all_black_pieces;
        uint64_t enemy_pieces = col ? all_black_pieces : all_white_pieces;
        uint64_t own_pawns = original->Board[0+6*!col];
        while(own_pawns)
        {
            int i=find_and_delete_trailling_1(own_pawns);
            if(col)
            score+=count(enemy_pieces & BP_template[i])*W.activity_pawn_attack+count(own_pieces & BP_template[i])*W.activity_pawn_defend;
            if(!col)
            score-=count(enemy_pieces & WP_template[i])*W.activity_pawn_attack+count(own_pieces & WP_template[i])*W.activity_pawn_defend;

            if(col && all_pieces & 1ULL << i+8)
            score+=W.activity_pawn_blocked;
            if(!col && all_pieces & 1ULL << i >> 8)
            score-=W.activity_pawn_blocked;
            
            //possible attacks, if pushed need to be awarded (from the starting rank also after the double push)
            uint64_t push_attacks = col ? (BP_template[i]<<8 | (i/8==1 ? BP_template[i]<<16 : 0)) : (WP_template[i]>>8 | (i/8==6 ? WP_template[i]>>16 : 0));
            if(col)
            score+=count(enemy_pieces & push_attacks)*W.activity_pawn_push_attack+count(own_pieces & push_attacks)*W.activity_pawn_push_defend;
            else
            score-=count(enemy_pieces & push_attacks)*W.activity_pawn_push_attack+count(own_pieces & push_attacks)*W.activity_pawn_push_defend;
        }
        
        // mobility: the squares a piece attacks that no own piece stands on, by its own table
        const int sign = 1-2*!col;
        uint64_t own_bishops = original->Board[3+6*!col];
        while(own_bishops)
        {
            int i=find_and_delete_trailling_1(own_bishops);
            uint64_t a = get_bishop_attacks(i,all_pieces);
            score+=sign*(count(own_pieces & a)*W.activity_bishop_defend + count(enemy_pieces & a)*W.activity_bishop_attack);
            const int m = count(a & ~own_pieces);
            mobility+=sign*(W.mobility_bishop_opening[m]*phase + W.mobility_bishop_endgame[m]*endgame);
        }
        uint64_t own_rooks = original->Board[1+6*!col];
        while(own_rooks)
        {
            int i=find_and_delete_trailling_1(own_rooks);
            uint64_t a = get_rook_attacks(i,all_pieces);
            score+=sign*(count(own_pieces & a)*W.activity_rook_defend + count(enemy_pieces & a)*W.activity_rook_attack);
            const int m = count(a & ~own_pieces);
            mobility+=sign*(W.mobility_rook_opening[m]*phase + W.mobility_rook_endgame[m]*endgame);
        }
        uint64_t own_queens = original->Board[4+6*!col];
        while(own_queens)
        {
            int i=find_and_delete_trailling_1(own_queens);
            uint64_t a = get_bishop_attacks(i,all_pieces) | get_rook_attacks(i,all_pieces);
            score+=sign*(count(own_pieces & a)*W.activity_queen_defend + count(enemy_pieces & a)*W.activity_queen_attack);
            const int m = count(a & ~own_pieces);
            mobility+=sign*(W.mobility_queen_opening[m]*phase + W.mobility_queen_endgame[m]*endgame);
        }
        uint64_t own_knights = original->Board[2+6*!col];
        while(own_knights)
        {
            int i=find_and_delete_trailling_1(own_knights);
            uint64_t a = Kn_template[i];
            score+=sign*(count(own_pieces & a)*W.activity_knight_defend + count(enemy_pieces & a)*W.activity_knight_attack);
            const int m = count(a & ~own_pieces);
            mobility+=sign*(W.mobility_knight_opening[m]*phase + W.mobility_knight_endgame[m]*endgame);
        }
        uint64_t own_king = original->Board[5+6*!col];
        while(own_king)
        {
            int i=find_and_delete_trailling_1(own_king);
            score+=count(own_pieces & (K_template[i]))*(1-2*!col)*W.activity_king_defend;
            score+=count(enemy_pieces & (K_template[i]))*(1-2*!col)*W.activity_king_attack;
        }

    }
    return score + mobility/PHASE_MAX;

}

float distance_to_king(int king_sq, int other_sq)// returns the distance of a sqare, to the kings square
{
    float king_row = king_sq/8;
    float king_col = king_sq%8;
    float other_row = other_sq/8;
    float other_col = other_sq%8;
    return sqrt(pow(king_row-other_row,2)+ pow(king_col-other_col,2));
}

int king_safety_of_colour(const BB* const original,bool white, const WEIGHTS& W )
{
    const uint64_t* Board = original->Board;
    float score=W.defensive_value[5];//the king can always defend itself
    int king_sq=__builtin_ctzll(Board[5+6*!white]);
    // #34 (branch-free king safety): each piece's term goes into term[] by square, then
    // the terms are added in square order. That is the same float additions, in the same
    // order, as the old single pass over the occupancy that tested each square against
    // every board in an if/else chain (score -= x is score += -x exactly).
    // The own king adds nothing, so it has no term.
    float term[64];
    for(int piece=0;piece<6;piece++)
    {
        uint64_t own = piece<5 ? Board[piece+6*!white] : 0;//the own king is always at distance 0, thats why it cannot defend itself with distance 0
        while(own)
        {
            int i=find_and_delete_trailling_1(own);
            term[i] = W.defensive_value[piece]/distance_to_king(king_sq,i);
        }
        uint64_t enemy = Board[piece+6*white];
        while(enemy)
        {
            int i=find_and_delete_trailling_1(enemy);
            term[i] = -(W.offensive_value[piece]/distance_to_king(king_sq,i));
        }
    }
    uint64_t occupancy = original->get_occupancy() & ~Board[5+6*!white];
    while(occupancy)
    {
        int i=find_and_delete_trailling_1(occupancy);
        score += term[i];
    }
// Old comment, from inside the per-square loop of the if/else-chain version that
// "#34 (branch-free king safety)" above replaced; metric was distance_to_king(king_sq,i).
/*
        if(own_captures & 1Ull << i)
        score += 200/metric*(2-1*white);
        if(enemy_captures & 1Ull << i)
        score -= 500/metric*(2-1*white);
        if(own_attacks & 1Ull << i)
        score += 100/metric*(2-1*white);
        if(enemy_attacks & 1Ull << i)
        score -= 300/metric*(2-1*white);
*/
    if(score>0)//king is safe
    return W.king_safety_value*sqrt(score);
    return W.king_safety_value*score*100;
    
    return score;
    
}

inline int material_eval(const BB* const original, const WEIGHTS& W)
{
    const int phase = game_phase(original), endgame = PHASE_MAX-phase;
    int score=0;
    for(int i=0;i<5;i++)// the kings cancel
    score += (W.piece_value[i]*phase + W.piece_value_endgame[i]*endgame)*(count(original->Board[i])-count(original->Board[i+6]));
    return score/PHASE_MAX;
}

int pawn_struckture_eval_of_colour(const BB* const original, bool white, const WEIGHTS& W)//positive is good for both colours
{
    int score=0;

    uint64_t pawns = original->Board[0+6*!white];
    uint64_t virtual_pawns = pawns;
    //punish pawn doubles and triples
    for(int j=0;j<8;j++)
    {
        int doubled_pawn_counter = count(pawns & mask_column[j]);
        if(doubled_pawn_counter>1)
        score -= W.punishment_for_double_pawn;
        if(doubled_pawn_counter>2)
        score -= W.punishment_for_trippled_pawn;
    }
    
    while(virtual_pawns)
    {
        int i=find_and_delete_trailling_1(virtual_pawns);
        //punish isolated pawns
        int column = i%8;
        if(column==0)
        {
            if(!(mask_column[column+1] & pawns))
            score -= W.punishment_for_isolated_pawn;
        }
        else if(column==7)
        {
            if(!(mask_column[column-1] & pawns))
            score -= W.punishment_for_isolated_pawn;
        }
        else
        {
            if(!(mask_column[column-1] & pawns) && !(mask_column[column+1] & pawns))
            score -= W.punishment_for_isolated_pawn;
        }

    }

    return score;
}

int positional_eval(const BB* const original, const WEIGHTS& W)
{
    int score=0;
    score += pawn_struckture_eval_of_colour(original,true,W)-pawn_struckture_eval_of_colour(original,false,W);
    return score;

}

uint64_t passed_pawns_of_colour(const BB* const original, bool white)
{
    const uint64_t pawns = original->Board[0+6*!white];
    const uint64_t enemy = original->Board[0+6*white];
    uint64_t virtual_pawns = pawns, passed = 0;
    while(virtual_pawns)
    {
        const int i = find_and_delete_trailling_1(virtual_pawns);
        const int column = i%8, rank = i/8;
        uint64_t files = mask_column[column];
        if(column>0) files |= mask_column[column-1];
        if(column<7) files |= mask_column[column+1];
        const uint64_t ahead = white ? (~0ULL << (8*(rank+1))) : ((1ULL << (8*rank))-1);// rank 7 white: shift 64 is UB, but a white pawn never stands there
        if(!(enemy & files & ahead)) passed |= 1ULL<<i;
    }
    return passed;
}

int passed_pawn_bonus(int relative_rank, int phase, const WEIGHTS& W)
{
    return (W.passed_pawn_value_opening[relative_rank]*phase + W.passed_pawn_value[relative_rank]*(PHASE_MAX-phase))/PHASE_MAX;
}

static int square_distance(int a, int b)
{
    return std::max(std::abs(a/8-b/8), std::abs(a%8-b%8));
}

void passed_pawn_parts(const BB* const original, bool white, int sq, int phase, const WEIGHTS& W, int part[PASSER_PARTS])
{
    const int rank = sq/8, column = sq%8, r = white ? rank : 7-rank;// r: relative rank, 1..6
    const int endgame = PHASE_MAX-phase;// the modifiers are scaled by endgame/PHASE_MAX
    const uint64_t occupancy = original->get_occupancy();
    const uint64_t path = mask_column[column] & (white ? (~0ULL << (8*(rank+1))) : ((1ULL << (8*rank))-1));
    const int stop = white ? sq+8 : sq-8;
    const int own_king = __builtin_ctzll(original->Board[5+6*!white]), enemy_king = __builtin_ctzll(original->Board[5+6*white]);
    for(int k=0;k<PASSER_PARTS;k++) part[k] = 0;

    part[PASSER_BASE] = passed_pawn_bonus(r, phase, W);

    const bool free_path = !(occupancy & path);
    if(free_path)
    part[PASSER_PATH] = W.passed_free_path[r]*endgame/PHASE_MAX;

    part[PASSER_KING] = (W.passed_king_enemy[r]*std::min(square_distance(enemy_king, stop), 5)
                       - W.passed_king_own[r]*std::min(square_distance(own_king, stop), 5))*endgame/PHASE_MAX;

    uint64_t neighbours = 0;
    if(column>0) neighbours |= mask_column[column-1];
    if(column<7) neighbours |= mask_column[column+1];
    const uint64_t beside_or_behind = (0xFFULL << (8*rank)) | (0xFFULL << (8*(white ? rank-1 : rank+1)));
    if(original->Board[0+6*!white] & neighbours & beside_or_behind)
    part[PASSER_SUPPORT] = W.passed_supported[r]*endgame/PHASE_MAX;

    for(int behind = white ? sq-8 : sq+8; behind>=0 && behind<64; behind += white ? -8 : 8)
    {
        if(!(occupancy & (1ULL<<behind)))
        continue;
        if(original->Board[1+6*!white] & (1ULL<<behind))
        part[PASSER_ROOK] = W.passed_rook_behind*endgame/PHASE_MAX;
        else if(original->Board[1+6*white] & (1ULL<<behind))
        part[PASSER_ROOK] = -W.passed_rook_behind*endgame/PHASE_MAX;
        break;
    }

    const int e = 6*white;// the enemy's pieces
    if(free_path && !(original->Board[1+e] | original->Board[2+e] | original->Board[3+e] | original->Board[4+e]))
    {
        const int promotion = white ? 56+column : column;
        const int pawn_moves = (7-r) - (r==1);// a pawn on its first rank steps two
        const int king_moves = square_distance(enemy_king, promotion) - (original->white_move != white);
        if(king_moves > pawn_moves)
        part[PASSER_SQUARE] = W.passed_unstoppable;
    }
}

int passed_pawn_eval(const BB* const original, const WEIGHTS& W)
{
    const int phase = game_phase(original);
    int score = 0;
    for(int white=0;white<2;white++)
    {
        uint64_t passed = passed_pawns_of_colour(original, white);
        int side = 0, square = 0;
        while(passed)
        {
            const int i = find_and_delete_trailling_1(passed);
            int part[PASSER_PARTS];
            passed_pawn_parts(original, white, i, phase, W, part);
            side += part[PASSER_BASE] + part[PASSER_PATH] + part[PASSER_KING] + part[PASSER_SUPPORT] + part[PASSER_ROOK];
            square = std::max(square, part[PASSER_SQUARE]);
        }
        side += square;
        score += white ? side : -side;
    }
    return score;
}

uint64_t pawn_attacks(uint64_t pawns, bool white)
{
    return white ? ((pawns & ~mask_column[0]) << 7) | ((pawns & ~mask_column[7]) << 9)
                 : ((pawns & ~mask_column[7]) >> 7) | ((pawns & ~mask_column[0]) >> 9);
}

// Backward and phalanx pawns (#90). Like piecetable(), summed in PHASE_MAXths and
// divided once.
int pawn_shape_eval(const BB* const original, const WEIGHTS& W)
{
    const int phase = game_phase(original), endgame = PHASE_MAX-phase;
    int score=0;
    for(int white=0;white<2;white++)
    {
        const int sign = white ? 1 : -1;
        const uint64_t pawns = original->Board[0+6*!white];
        const uint64_t enemy_attacks = pawn_attacks(original->Board[0+6*white], !white);
        uint64_t virtual_pawns = pawns;
        while(virtual_pawns)
        {
            const int i = find_and_delete_trailling_1(virtual_pawns);
            const int column = i%8, rank = i/8, r = white ? rank : 7-rank;
            // a pair side by side counts once, at its left pawn
            if(column<7 && (pawns >> (i+1) & 1))
            score += sign*(W.pawn_phalanx_opening[r]*phase + W.pawn_phalanx_endgame[r]*endgame);
            // backward: not isolated, but every own pawn on a neighbouring file is ahead of it,
            // so none can come to protect it, and an enemy pawn stops it
            uint64_t neighbours = 0;
            if(column>0) neighbours |= mask_column[column-1];
            if(column<7) neighbours |= mask_column[column+1];
            const uint64_t level_or_behind = white ? (~0ULL >> (8*(7-rank))) : (~0ULL << (8*rank));
            const int stop = white ? i+8 : i-8;
            if((pawns & neighbours) && !(pawns & neighbours & level_or_behind) && (enemy_attacks >> stop & 1))
            score += sign*(W.pawn_backward_opening*phase + W.pawn_backward_endgame*endgame);
        }
    }
    return score/PHASE_MAX;
}

// The bishop pair, rooks on open and half-open files and on the 7th, and knights and
// bishops on outposts (#90). Summed in PHASE_MAXths and divided once.
int placement_eval(const BB* const original, const WEIGHTS& W)
{
    const uint64_t* B = original->Board;
    const int phase = game_phase(original), endgame = PHASE_MAX-phase;
    int score=0;
    for(int white=0;white<2;white++)
    {
        const int sign = white ? 1 : -1, own = 6*!white, enemy = 6*white;
        const uint64_t own_pawns = B[0+own], enemy_pawns = B[0+enemy];
        if(count(B[3+own])>=2)
        score += sign*(W.bishop_pair_opening*phase + W.bishop_pair_endgame*endgame);

        const uint64_t seventh = mask_row[white ? 6 : 1], eighth = mask_row[white ? 7 : 0];
        uint64_t rooks = B[1+own];
        while(rooks)
        {
            const int i = find_and_delete_trailling_1(rooks);
            const uint64_t file = mask_column[i%8];
            if(!(file & (own_pawns|enemy_pawns)))
            score += sign*(W.rook_open_file_opening*phase + W.rook_open_file_endgame*endgame);
            else if(!(file & own_pawns))
            score += sign*(W.rook_semi_open_file_opening*phase + W.rook_semi_open_file_endgame*endgame);
            if((seventh >> i & 1) && ((enemy_pawns & seventh) || (B[5+enemy] & eighth)))
            score += sign*(W.rook_seventh_opening*phase + W.rook_seventh_endgame*endgame);
        }

        // an outpost: relative rank 4-6, protected by an own pawn, and no enemy pawn on a
        // neighbouring file ahead of it, so none can ever attack it
        uint64_t minors = (B[2+own] | B[3+own]) & pawn_attacks(own_pawns, white) & (white ? 0x0000FFFFFF000000ULL : 0x000000FFFFFF0000ULL);
        while(minors)
        {
            const int i = find_and_delete_trailling_1(minors);
            const int column = i%8, rank = i/8;
            uint64_t neighbours = 0;
            if(column>0) neighbours |= mask_column[column-1];
            if(column<7) neighbours |= mask_column[column+1];
            const uint64_t ahead = white ? (~0ULL << (8*(rank+1))) : ((1ULL << (8*rank))-1);
            if(enemy_pawns & neighbours & ahead)
            continue;
            if(B[2+own] >> i & 1)
            score += sign*(W.outpost_knight_opening*phase + W.outpost_knight_endgame*endgame);
            else
            score += sign*(W.outpost_bishop_opening*phase + W.outpost_bishop_endgame*endgame);
        }
    }
    return score/PHASE_MAX;
}

// Enemy pieces attacked by pawns, by knights and bishops, and by rooks, by the victim's
// type, and every enemy piece but the king attacked and not defended (#90).
int threat_eval(const BB* const original, const WEIGHTS& W)
{
    const uint64_t* B = original->Board;
    const uint64_t occupancy = original->get_occupancy();
    uint64_t by_pawn[2], by_minor[2], by_rook[2], attacks[2];
    for(int white=0;white<2;white++)
    {
        const int own = 6*!white;
        uint64_t minor = 0, rook = 0, other = 0, bb;
        bb = B[2+own];
        while(bb) minor |= Kn_template[find_and_delete_trailling_1(bb)];
        bb = B[3+own];
        while(bb) minor |= get_bishop_attacks(find_and_delete_trailling_1(bb), occupancy);
        bb = B[1+own];
        while(bb) rook |= get_rook_attacks(find_and_delete_trailling_1(bb), occupancy);
        bb = B[4+own];
        while(bb)
        {
            const int i = find_and_delete_trailling_1(bb);
            other |= get_bishop_attacks(i, occupancy) | get_rook_attacks(i, occupancy);
        }
        if(B[5+own])
        other |= K_template[__builtin_ctzll(B[5+own])];
        by_pawn[white] = pawn_attacks(B[0+own], white);
        by_minor[white] = minor;
        by_rook[white] = rook;
        attacks[white] = by_pawn[white] | minor | rook | other;
    }
    int score=0;
    for(int white=0;white<2;white++)
    {
        const int enemy = 6*white;
        int side = 0;
        for(int type=0;type<5;type++)
        {
            const uint64_t victims = B[type+enemy];
            side += count(victims & by_pawn[white])*W.threat_by_pawn[type]
                  + count(victims & by_minor[white])*W.threat_by_minor[type]
                  + count(victims & by_rook[white])*W.threat_by_rook[type]
                  + count(victims & attacks[white] & ~attacks[!white])*W.threat_hanging;
        }
        score += white ? side : -side;
    }
    return score;
}

// The side to move is worth a tempo (#70). Without it the eval gave the side
// that just moved too much: tools/tempo_swing measures that as half the odd/even
// swing of the root score (net off), from W.tempo_endgame with bare kings
// to W.tempo_opening with all pieces on the board.
int tempo_eval(const BB* const original, const WEIGHTS& W)
{
    const int phase = game_phase(original);
    int tempo = (W.tempo_opening*phase + W.tempo_endgame*(PHASE_MAX-phase))/PHASE_MAX;
    return original->white_move ? tempo : -tempo;
}

int basic_eval(const BB*const original , const WEIGHTS& W)// return the evaluation in centipawns
{
    int score=0;
    
    score += material_eval(original,W);
    score += piecetable(original,W);
    
    score += king_safety_eval(original,W);
    //return score;
    //score=score*0.1; //games get fun, when they DO NOT CARE ABOUT MATERIAL

    score += positional_eval(original,W);
    score += passed_pawn_eval(original,W);
    score += pawn_shape_eval(original,W);
    score += placement_eval(original,W);
    score += threat_eval(original,W);


    score += piece_activity_eval(original,W);

    score += tempo_eval(original,W);

    return score;
}

int tactical_potential(const BB* const original, int king_safety_white, int king_safety_black, const WEIGHTS& W)
{
    if(original==NULL)
    {
        std::cout << "Error in tactical potential\n";
        return 0;
    }
    const uint64_t* Board = original->Board;
    uint64_t white_pieces = original->get_pieces_of_colour(true);
    uint64_t black_pieces = original->get_pieces_of_colour(false);
    int score=0;
    // king_safety_white/king_safety_black are king_safety_of_both()'s (<= 0, 0 = safe), computed once
    // by the caller (sorting_eval already needs both) instead of redone here.
    if(king_safety_white<0)
    score-=king_safety_white;
    if(king_safety_black<0)
    score-=king_safety_black;
    uint64_t attacks_white = original->get_attacked_squares(1);
    uint64_t attacks_black = original->get_attacked_squares(0);
    uint64_t white_captures = attacks_white & black_pieces;
    uint64_t black_captures = attacks_black & white_pieces;
    uint64_t white_defends = attacks_white & white_pieces;
    uint64_t black_defends = attacks_black & black_pieces;
    attacks_white = attacks_white & ~white_captures;//remove captures
    attacks_black = attacks_black & ~black_captures;
    //todo make a more elaborate functionality, that assigns a value, per attacked square, eg an array of 64 values, that are added to the score
    uint64_t shared_attacks = attacks_white & attacks_black;
    uint64_t shared_captures = (attacks_white & black_captures) | (attacks_black & white_captures);
    score += 150*count(shared_attacks);
    score += 200*count(shared_captures);
    //std::cout << "score after shared attacks: " << score << std::endl;


    uint64_t virtual_white_captures = white_captures;
    int pos_black_king = __builtin_ctzll(Board[5+6]);
    //std::cout << "Tactical potential 3: " << score << std::endl;
    while(virtual_white_captures)
    {
        int i=find_and_delete_trailling_1(virtual_white_captures);
        int piecetype=-1;
        for(int j=0;j<6;j++)
        {
            if(Board[j+6] & 1Ull << i)
            piecetype=j;
        }
        if(piecetype==-1)
        {
            std::cout << "Error in tactical potential\n";
            return 0;
        }
        score += W.piece_value[piecetype]/3;
        score += 1/(1+distance_to_king(pos_black_king,i))*50;
    }
   // std::cout << "score after white captures: " << score << std::endl;
    uint64_t virtual_black_captures = black_captures;
    int pos_white_king = __builtin_ctzll(Board[5]);
    //std::cout << "Tactical potential 2: " << score << std::endl;
    while(virtual_black_captures)
    {
        int i=find_and_delete_trailling_1(virtual_black_captures);
        int piecetype=-1;
        for(int j=6;j<12;j++)
        {
            if(Board[j-6] & 1Ull << i)
            piecetype=j-6;
        }
        if(piecetype==-1)
        {
            std::cout << "Error in tactical potential\n";
            return 0;
        }
        score += W.piece_value[piecetype]/3;
        score += 1/(1+distance_to_king(pos_white_king,i))*50;
    }
   // std::cout << "score after black captures: " << score << std::endl;

    

    //return tanh(score/1000.0)*1000;
    //std::cout << "Tactical potential 1: " << score << std::endl;
    int return_value = std::min(std::max(200,score),int(tanh(score/4000.0)*1000));//this function looks good(its graph) 4000 will need adjustment, as the function above changes. currently all moves seem to be about 3500-4000, wich is ofc not enough vriation
    if(return_value<0)
    {
        std::cout << "Tactical potential: " << return_value << std::endl;
        std::cout << "Thats an error\n";
        print(Board);
        exit(1);
    }
    if(return_value>100000)
    {
        std::cout << "Tactical potential: " << return_value << std::endl;
        std::cout << "Thats an error\n";
        print(Board);
        exit(1);
    }
    //std::cout << "Tactical potential: " << return_value << std::endl;
    return return_value;
}

int sorting_eval(const BB* const original, const WEIGHTS& W )// accelerates pruning this function has to be lightheaded(Quick to compute)
{
    //return basic_eval(original,W);
    int score=0;
    score +=material_eval(original,W);
    // king_safety_of_colour(white) and (black) computed once here and reused below for
    // tactical_potential, instead of each being computed twice more inside it.
    int king_safety_white, king_safety_black;// <= 0 each, 0 = safe (#42)
    king_safety_of_both(original,king_safety_white,king_safety_black,W);
    int king_s = original->white_move ? king_safety_black : king_safety_white;//king_safety_of_colour(!original->white_move)
    if(king_s<=0)//if the king may be in danger, we must attack!//is this even quicker? in a queen vs king endgame with gave 30% more pruning
    score -= W.value_of_king_safety_for_sorting*king_s*(1-2*!original->white_move);
    king_s = original->white_move ? king_safety_white : king_safety_black;//king_safety_of_colour(original->white_move)
    if(king_s<=0)
    score += W.value_of_king_safety_for_sorting*king_s*(1-2*!original->white_move);
    //score +=original->Board[2+6]%1000;
    score += piecetable(original,W);// is this too slow??// if i add this pruning is more inefficiient?? what the fuck?? in all tested scenarios this is bad
    //if(in_check((*original).Board,(*original).white_move))
    //score .check_value*(1-2*!(*original).white_move);
    score+=tactical_potential(original,king_safety_white,king_safety_black,W)/10*(1-2*!original->white_move);//77% without this
    score+=piece_activity_eval(original,W);
    return score;
}





















#endif // BASIC_EVAL_CPP
