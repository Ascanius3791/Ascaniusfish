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

int piecetable(const BB* const original , const WEIGHTS& W)
{
    // Exact: the phase weights are k/39, so the sum is kept in 39ths as an
    // integer and divided once. That makes the result colour-symmetric (int
    // division truncates toward zero) and independent of summation order.
    int score=0;
    int EW[2],OW[2];//endgame weight, opening weight, in 39ths
    OW[1]=enemy_material_left_39ths(original,1);//0=black, 1 = white
    OW[0]=enemy_material_left_39ths(original,0);
    EW[0]=MATERIAL_MAX-OW[0];
    EW[1]=MATERIAL_MAX-OW[1];

    // Original summed square-by-square (exactly one piece per square), so the
    // float accumulation order was strictly increasing square index. Summing
    // grouped by piece type instead is mathematically equivalent but not
    // bit-identical (float addition isn't associative, and the result gets
    // truncated to int), so piece_at[] rebuilds the per-square order while
    // still avoiding the original's 12-branch-per-square scan.
    int piece_at[64];
    for(int i=0;i<64;i++) piece_at[i]=-1;
    uint64_t all_pieces=0;
    for(int piece=0; piece<12; piece++)
    {
        uint64_t bb = original->Board[piece];
        all_pieces |= bb;
        while(bb)
        {
            int i=find_and_delete_trailling_1(bb);
            piece_at[i]=piece;
        }
    }
    while(all_pieces)
    {
        int i=find_and_delete_trailling_1(all_pieces);
        int piece=piece_at[i];
        if(piece<6)
        {
            score += W.piece_table_value_opening[piece][i]*OW[1]+W.piece_table_value_endgame[piece][i]*EW[1];
        }
        else
        {
            piece-=6;
            score -= W.piece_table_value_opening[piece][i^56]*OW[0]+W.piece_table_value_endgame[piece][i^56]*EW[0];// black reads white's table rank-flipped (#85)
        }
    }


    return score/MATERIAL_MAX;
}

int piece_activity_eval(const BB* const original, const WEIGHTS& W)
{
    int score=0;
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
        
        uint64_t own_bishops = original->Board[3+6*!col]|original->Board[4+6*!col];
        while(own_bishops)
        {
            int i=find_and_delete_trailling_1(own_bishops);
            uint64_t bishop_attacks = get_bishop_attacks(i,all_pieces);
            score+=count(own_pieces & bishop_attacks)*W.activity_bishop_defend*(1-2*!col);
            score+=count(enemy_pieces & bishop_attacks)*W.activity_bishop_attack*(1-2*!col);
            score+=count(bishop_attacks)*(1-2*!col)*W.activity_bishop_square;
        }
        uint64_t own_rooks = original->Board[1+6*!col]|original->Board[4+6*!col];
        while(own_rooks)
        {
            int i=find_and_delete_trailling_1(own_rooks);
            uint64_t rook_attacks = get_rook_attacks(i,all_pieces);
            score+=count(own_pieces & rook_attacks)*(1-2*!col)*W.activity_rook_defend;
            score+=count(enemy_pieces & rook_attacks)*(1-2*!col)*W.activity_rook_attack;
            score+=count(rook_attacks)*(1-2*!col)*W.activity_rook_square;
        }
        uint64_t own_knights = original->Board[2+6*!col];
        while(own_knights)
        {
            int i=find_and_delete_trailling_1(own_knights);
            score+=count(own_pieces & (Kn_template[i]))*(1-2*!col)*W.activity_knight_defend;
            score+=count(enemy_pieces & (Kn_template[i]))*(1-2*!col)*W.activity_knight_attack;
            score+=count(Kn_template[i])*(1-2*!col)*W.activity_knight_square;
        }
        uint64_t own_king = original->Board[5+6*!col];
        while(own_king)
        {
            int i=find_and_delete_trailling_1(own_king);
            score+=count(own_pieces & (K_template[i]))*(1-2*!col)*W.activity_king_defend;
            score+=count(enemy_pieces & (K_template[i]))*(1-2*!col)*W.activity_king_attack;
        }

    }
    return score/2;//influece was too hard

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
    float score_W=0,score_B=0,material_left=0;
    for(int i=0;i<6;i++)
    {
        score_W += W.piece_value[i]*count(original->Board[i]);
        score_B += W.piece_value[i]*count(original->Board[i+6]);
    }
    material_left = score_W+score_B;
    return (int)((score_W-score_B)*sqrt(2-(8*1+3*4*2*5+9+3.5)*2/material_left));
}

int pawn_struckture_eval_of_colour(const BB* const original, bool white, const WEIGHTS& W)//positive is good for both colours
{
    int score=0;

    uint64_t occ_sq=0;//own pieces and pawns only: a pawn supports its own side (#36)
    for(int i=0;i<6;i++)
    {
        occ_sq |= original->Board[i+6*!white];
    }
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
        // reward pawn supporting something
        if(white)
        {
            score+=W.pawn_supporting_value*count(occ_sq & BP_template[i]);//count is 0 or 1 or 2
        }
        if(!white)
        {
            score+=W.pawn_supporting_value*count(occ_sq & WP_template[i]);
        }
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

int passed_pawn_bonus(int relative_rank, int OW, const WEIGHTS& W)
{
    return W.passed_pawn_value[relative_rank]*(OW + 2*(2*MATERIAL_MAX-OW))/(4*MATERIAL_MAX);
}

static int square_distance(int a, int b)
{
    return std::max(std::abs(a/8-b/8), std::abs(a%8-b%8));
}

void passed_pawn_parts(const BB* const original, bool white, int sq, int OW, const WEIGHTS& W, int part[PASSER_PARTS])
{
    const int rank = sq/8, column = sq%8, r = white ? rank : 7-rank;// r: relative rank, 1..6
    const int endgame = 2*MATERIAL_MAX-OW;// the modifiers are scaled by endgame/(2*MATERIAL_MAX)
    const uint64_t occupancy = original->get_occupancy();
    const uint64_t path = mask_column[column] & (white ? (~0ULL << (8*(rank+1))) : ((1ULL << (8*rank))-1));
    const int stop = white ? sq+8 : sq-8;
    const int own_king = __builtin_ctzll(original->Board[5+6*!white]), enemy_king = __builtin_ctzll(original->Board[5+6*white]);
    for(int k=0;k<PASSER_PARTS;k++) part[k] = 0;

    part[PASSER_BASE] = passed_pawn_bonus(r, OW, W);

    const bool free_path = !(occupancy & path);
    if(free_path)
    part[PASSER_PATH] = W.passed_free_path[r]*endgame/(2*MATERIAL_MAX);

    part[PASSER_KING] = (W.passed_king_enemy[r]*std::min(square_distance(enemy_king, stop), 5)
                       - W.passed_king_own[r]*std::min(square_distance(own_king, stop), 5))*endgame/(2*MATERIAL_MAX);

    uint64_t neighbours = 0;
    if(column>0) neighbours |= mask_column[column-1];
    if(column<7) neighbours |= mask_column[column+1];
    const uint64_t beside_or_behind = (0xFFULL << (8*rank)) | (0xFFULL << (8*(white ? rank-1 : rank+1)));
    if(original->Board[0+6*!white] & neighbours & beside_or_behind)
    part[PASSER_SUPPORT] = W.passed_supported[r]*endgame/(2*MATERIAL_MAX);

    for(int behind = white ? sq-8 : sq+8; behind>=0 && behind<64; behind += white ? -8 : 8)
    {
        if(!(occupancy & (1ULL<<behind)))
        continue;
        if(original->Board[1+6*!white] & (1ULL<<behind))
        part[PASSER_ROOK] = W.passed_rook_behind*endgame/(2*MATERIAL_MAX);
        else if(original->Board[1+6*white] & (1ULL<<behind))
        part[PASSER_ROOK] = -W.passed_rook_behind*endgame/(2*MATERIAL_MAX);
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
    const int OW = std::min(enemy_material_left_39ths(original,1)+enemy_material_left_39ths(original,0), 2*MATERIAL_MAX);
    int score = 0;
    for(int white=0;white<2;white++)
    {
        uint64_t passed = passed_pawns_of_colour(original, white);
        int side = 0, square = 0;
        while(passed)
        {
            const int i = find_and_delete_trailling_1(passed);
            int part[PASSER_PARTS];
            passed_pawn_parts(original, white, i, OW, W, part);
            side += part[PASSER_BASE] + part[PASSER_PATH] + part[PASSER_KING] + part[PASSER_SUPPORT] + part[PASSER_ROOK];
            square = std::max(square, part[PASSER_SQUARE]);
        }
        side += square;
        score += white ? side : -side;
    }
    return score;
}

// The side to move is worth a tempo (#70). Without it the eval gave the side
// that just moved too much: tools/tempo_swing measures that as half the odd/even
// swing of the root score (net off), from W.tempo_endgame with bare kings
// to W.tempo_opening with all material on the board.
int tempo_eval(const BB* const original, const WEIGHTS& W)
{
    int OW = std::min(enemy_material_left_39ths(original,1)+enemy_material_left_39ths(original,0), 2*MATERIAL_MAX);//both sides' material, 0..78
    int tempo = (W.tempo_opening*OW + W.tempo_endgame*(2*MATERIAL_MAX-OW))/(2*MATERIAL_MAX);
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

    
    score+= W.mobility_value*(count(original->get_attacked_squares(1))-count(original->get_attacked_squares(0)));
    
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
