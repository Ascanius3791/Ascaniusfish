#ifndef BASIC_EVAL_CPP
#define BASIC_EVAL_CPP
#include "../lib/basic_eval.hpp"


float enemy_material_left_percent(const BB* const original, bool for_white)//
{
    float score=0, max_score =8*1+5*2+3*4+9*1;//8 pawns, 2 rooks, 4 bishops/knights, 1 queen
    score += 1*count(original->Board[0+6*for_white]);
    score += 5*count(original->Board[1+6*for_white]);
    score += 3*count(original->Board[2+6*for_white]);
    score += 3*count(original->Board[3+6*for_white]);
    score += 9*count(original->Board[4+6*for_white]);
    return score/max_score;
}

int piecetable(const BB* const original , const WEIGHTS W)
{
    float score=0;
    float EW[2],OW[2];//endgame weight, opening weight
    OW[1]=enemy_material_left_percent(original,1);//0=black, 1 = white
    OW[0]=enemy_material_left_percent(original,0);
    EW[0]=1-OW[0];
    EW[1]=1-OW[1];
    uint64_t all_pieces= original->Board[0]|original->Board[1]|original->Board[2]|original->Board[3]|original->Board[4]|original->Board[5]|original->Board[6]|original->Board[7]|original->Board[8]|original->Board[9]|original->Board[10]|original->Board[11];
    while(all_pieces)
    {
        int i=find_and_delete_trailling_1(all_pieces);
        //pawns
        

        
        if(original->Board[0] & 1Ull << i)
        score += W.piece_table_value_opening[0][i]*OW[1]+W.piece_table_value_endgame[0][i]*EW[1];
        if(original->Board[6] & 1Ull << i)
        score -= W.piece_table_value_opening[6][i]*OW[0]+W.piece_table_value_endgame[6][i]*EW[0];
        //other pieces
        continue;
        for(int col =0;col<2;col++)
        for(int piec=1;piec<6;piec++)
        {
            if(original->Board[piec+6*!col] & 1Ull << i)
            score += (1-2*!col)*(W.piece_table_value_opening[piec][i]*OW[col]+W.piece_table_value_endgame[piec][i]*EW[col]);
        }
    }
        
            
    return score;
}

int piece_activity_eval(const BB* const original, const WEIGHTS W)
{
    int score=0;
    uint64_t all_black_pieces= original->Board[6]|original->Board[7]|original->Board[8]|original->Board[9]|original->Board[10]|original->Board[11];
    uint64_t all_white_pieces= original->Board[0]|original->Board[1]|original->Board[2]|original->Board[3]|original->Board[4]|original->Board[5];
    uint64_t all_pieces= all_black_pieces|all_white_pieces;
    for(int col=0;col<2;col++)
    {
        uint64_t own_pieces = original->Board[0+6*!col]|original->Board[1+6*!col]|original->Board[2+6*!col]|original->Board[3+6*!col]|original->Board[4+6*!col]|original->Board[5+6*!col];
        uint64_t enemy_pieces = original->Board[0+6*col]|original->Board[1+6*col]|original->Board[2+6*col]|original->Board[3+6*col]|original->Board[4+6*col]|original->Board[5+6*col];
        uint64_t own_pawns = original->Board[0+6*!col];
        uint64_t enemy_pawns = original->Board[0+6*col];
        
        uint64_t white_pawn_attacks;
        uint64_t black_pawn_attacks;
        
        
        
        own_pawns = original->Board[0+6*!col];
        while(own_pawns)
        {
            int i=find_and_delete_trailling_1(own_pawns);
            if(col)
            score+=count(all_pieces & BP_template[i])*30;//*W.piece_activity_value[0];
            if(!col)
            score-=count(all_pieces & WP_template[i])*30;//*W.piece_activity_value[0];

            if(col && all_pieces & 1ULL << i+8)
            score-=20;
            if(!col && all_pieces & 1ULL << i >> 8)
            score+=20;
            
            //possible attacks, if pushed need to be awarded
            if(col)
            score+=count(all_pieces & BP_template[i]>>8)*20;//*W.piece_activity_value[0];
            else
            score-=count(all_pieces & WP_template[i]<<8)*20;//*W.piece_activity_value[0];
        }
        
        uint64_t own_bishops = original->Board[3+6*!col]|original->Board[4+6*!col];
        while(own_bishops)
        {
            int i=find_and_delete_trailling_1(own_bishops);
            uint64_t bishop_attacks = get_bishop_attacks(i,all_pieces);
            score+=count(own_pieces & bishop_attacks*(1-2*!col))*10;//*W.piece_activity_value[3];
            score+=count(enemy_pieces & bishop_attacks*(1-2*!col))*40;//*W.piece_activity_value[3];
            score+=count(bishop_attacks)*(1-2*!col)*5;
        }
        uint64_t own_rooks = original->Board[1+6*!col]|original->Board[4+6*!col];
        while(own_rooks)
        {
            int i=find_and_delete_trailling_1(own_rooks);
            uint64_t rook_attacks = get_rook_attacks(i,all_pieces);
            score-=count(own_pieces & rook_attacks)*(1-2*!col)*10;//*W.piece_activity_value[1];
            score+=count(enemy_pieces & rook_attacks)*(1-2*!col)*40;//*W.piece_activity_value[1];
            score+=count(rook_attacks)*(1-2*!col)*7;
        }
        uint64_t own_knights = original->Board[2+6*!col];
        while(own_knights)
        {
            int i=find_and_delete_trailling_1(own_knights);
            score-=count(own_pieces & (Kn_template[i]))*(1-2*!col)*10;//*W.piece_activity_value[2];
            score+=count(enemy_pieces & (Kn_template[i]))*(1-2*!col)*10;//*W.piece_activity_value[2];
            score+=count(Kn_template[i])*(1-2*!col)*5;
        }
        uint64_t own_king = original->Board[5+6*!col];
        while(own_king)
        {
            int i=find_and_delete_trailling_1(own_king);
            score+=count(own_pieces & (K_template[i]))*(1-2*!col)*15;//*W.piece_activity_value[5];
            score+=count(enemy_pieces & (K_template[i]))*(1-2*!col)*20;//*W.piece_activity_value[5];
        }

    }
    return score/2;//influece was to hard

}

float distance_to_king(int king_sq, int other_sq)// returns the distance of a sqare, to the kings square
{
    float king_row = king_sq/8;
    float king_col = king_sq%8;
    float other_row = other_sq/8;
    float other_col = other_sq%8;
    return sqrt(pow(king_row-other_row,2)+ pow(king_col-other_col,2));
}

int king_safety_of_colour(const uint64_t Board[12],bool white, const WEIGHTS W )
{   
    
    float score=W.defensive_value[5];//the king can always defend itself
    int king_sq=__builtin_ctzll(Board[5+6*!white]);
    uint64_t occupancy = Board[0]|Board[1]|Board[2]|Board[3]|Board[4]|Board[5]|Board[6]|Board[7]|Board[8]|Board[9]|Board[10]|Board[11]; 
    uint64_t own_P = Board[0+6*!white]|Board[1+6*!white]|Board[2+6*!white]|Board[3+6*!white]|Board[4+6*!white]|Board[5+6*!white];
    uint64_t enemy_P = Board[0+6*white]|Board[1+6*white]|Board[2+6*white]|Board[3+6*white]|Board[4+6*white]|Board[5+6*white];


    uint64_t enemy_attacks=attacked_squares(Board,!white);   
    uint64_t enemy_captures=enemy_attacks & own_P;
    uint64_t own_attacks=attacked_squares(Board,white);
    uint64_t own_captures=own_attacks & enemy_P;
    while(occupancy)
    {
        int i=find_and_delete_trailling_1(occupancy);
        float metric = distance_to_king(king_sq,i);//experimental
        if(metric ==0) //should be redundant
        metric=1;

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
        if(Board[0+6*!white] & 1Ull << i)
        score += W.defensive_value[0]/metric;
        else if(Board[0+6*white] & 1Ull << i)
        score -= W.offensive_value[0]/metric;

        else if(Board[1+6*!white] & 1Ull << i)
        score += W.defensive_value[1]/metric;
        else if(Board[1+6*white] & 1Ull << i)
        score -= W.offensive_value[1]/metric;

        else if(Board[2+6*!white] & 1Ull << i)
        score += W.defensive_value[2]/metric;
        else if(Board[2+6*white] & 1Ull << i)
        score -= W.offensive_value[2]/metric;

        else if(Board[3+6*!white] & 1Ull << i)
        score += W.defensive_value[3]/metric;
        else if(Board[3+6*white] & 1Ull << i)
        score -= W.offensive_value[3]/metric;

        else if(Board[4+6*!white] & 1Ull << i)
        score += W.defensive_value[4]/metric;
        else if(Board[4+6*white] & 1Ull << i)
        score -= W.offensive_value[4]/metric;

//the own king is always at distance 0, thats why it cannot defend itself with distance 0
        else if(Board[5+6*white] & 1Ull << i)
        score -= W.offensive_value[5]/metric;
    }
    if(score>0)
    ;
    else
    ;//std::cout << "king is in danger: " << score << std::endl;
    if(score>0)//king is safe
    return W.king_safety_value*sqrt(score);
    return W.king_safety_value*score;
    
    return score;
    
}

inline int material_eval(const BB* const original, const WEIGHTS W)
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

int central_pawn_presence(const BB* const original, bool white, const WEIGHTS W)//positive is good for both colours
{
    int score=0;
    uint64_t mask_center = 0B0000000000000000001111000011110000111100001111000000000000000000;
    uint64_t central_pawns = original->Board[0+6*!white]&mask_center;
    uint64_t virtual_pawns = central_pawns;
    while(virtual_pawns)
    {
        int i=find_and_delete_trailling_1(virtual_pawns);
        uint64_t attacks = white ? BP_template[i] : WP_template[i]; 
        while(attacks)
        {
            int j=find_and_delete_trailling_1(attacks);
            score+=W.piece_table_value_opening[0][j];
        }
    }
    return score;
}

int pawn_struckture_eval_of_colour(const BB* const original, bool white, const WEIGHTS W)//positive is good for both colours
{
    int score=0;

    bool WM=original->white_move;
    score += central_pawn_presence(original,white,W);
    uint64_t occ_sq=0;
    for(int i=0;i<6;i++)
    {
        occ_sq |= original->Board[i+6*WM];
        occ_sq |= original->Board[i+6*!WM];
    }
    uint64_t pawns = original->Board[0+6*!white];
    uint64_t virtual_pawns = pawns;
    //punish pawn doubles and triples
    for(int j=0;j<8;j++)
    {
        
        int doubled_pawn_counter=0;
        for(int i=0;i<8;i++)
        {
            if(pawns & 1Ull << j+8*i)
            doubled_pawn_counter++;
        }
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
        continue;
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

int positional_eval(const BB* const original, const WEIGHTS W)
{
    int score=0;
    score += pawn_struckture_eval_of_colour(original,true,W)-pawn_struckture_eval_of_colour(original,false,W);
    return score;

}

int basic_eval(const BB*const original , const WEIGHTS W)// return the evaluation in centipawns
{
    int score=0;
    
    score += material_eval(original,W);
    score += king_safety_of_colour(original->Board,true,W);
    score -= king_safety_of_colour(original->Board,false,W);
    //score=score*0.1; //games get fun, when they DO NOT CARE ABOUT MATERIAL
    
    score += piecetable(original,W);//current testing
    //uint64_t occ = original->Board[0]|original->Board[1]|original->Board[2]|original->Board[3]|original->Board[4]|original->Board[5]|original->Board[6]|original->Board[7]|original->Board[8]|original->Board[9]|original->Board[10]|original->Board[11];
    //score+=occ%1000;//so test, if the board has changed

    score += positional_eval(original,W);
    
    
    score+= 5*(count(attacks_by_col(original->Board,1))-count(attacks_by_col(original->Board,0)));
    
    score += piece_activity_eval(original,W);

    return score;
}

int tactical_potential(const uint64_t Board[12], WEIGHTS W)
{
    if(Board==NULL)
    {
        std::cout << "Error in tactical potential\n";
        return 0;
    }
    uint64_t white_pieces = Board[0]|Board[1]|Board[2]|Board[3]|Board[4]|Board[5];
    uint64_t black_pieces = Board[6]|Board[7]|Board[8]|Board[9]|Board[10]|Board[11];
    uint64_t occupancy = white_pieces|black_pieces;
    int score=0;
    int king_s = king_safety_of_colour(Board,1,W);
    
    if(king_s<0)
    score-=king_s;
    //std::cout << "Tactical potential 5: " << score << std::endl;
    king_s = king_safety_of_colour(Board,0,W);
    
    if(king_s<0)
    score-=king_s;
    //std::cout << "Tactical potential 4: " << score << std::endl;
    uint64_t attacks_white = attacks_by_col(Board,1);
    uint64_t attacks_black = attacks_by_col(Board,0);
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

int sorting_eval(const BB* const original, const WEIGHTS W )// accelerates pruning this function has to be lightheaded(Quick to compute)
{
    //return basic_eval(original,W);
    int score=0;
    score +=material_eval(original,W);
    int king_s=king_safety_of_colour(original->Board,!original->white_move,W);
    if(king_s<=0)//if the king may be in danger, we must attack!//is this even quicker? in a queen vs king endgame with gave 30% more pruning
    score -= W.value_of_king_safety_for_sorting*king_s*(1-2*!original->white_move);
    king_s=king_safety_of_colour(original->Board,original->white_move,W);
    if(king_s<=0)
    score += W.value_of_king_safety_for_sorting*king_s*(1-2*!original->white_move);
    //score +=original->Board[2+6]%1000;
    score += piecetable(original,W);// is this too slow??// if i add this pruning is more inefficiient?? what the fuck?? in all tested scenarios this is bad
    //if(in_check((*original).Board,(*original).white_move))
    //score .check_value*(1-2*!(*original).white_move);
    score+=tactical_potential(original->Board,W)/10*(1-2*!original->white_move);//77% without this
    score+=piece_activity_eval(original,W);
    return score;
}





















#endif // BASIC_EVAL_CPP

