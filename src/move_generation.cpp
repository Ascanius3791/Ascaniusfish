#ifndef MOVE_GENERATION_CPP
#define MOVE_GENERATION_CPP

#include "../lib/move_generation.hpp"

int all_moves(const BB* const original, BB* const wfh , int len_wfh)// returns number of moves
{
    uint64_t Own_Pawns=original->Board[0+6*!original->white_move];
    uint64_t Own_Knights=original->Board[2+6*!original->white_move];
    uint64_t Own_Bishops=original->Board[3+6*!original->white_move]|original->Board[4+6*!original->white_move];//careful, there are to unify queen and bishop moves
    uint64_t Own_Rooks=original->Board[1+6*!original->white_move]|original->Board[4+6*!original->white_move];
    uint64_t Own_King=original->Board[5+6*!original->white_move];
    if(extensive_time_display)
    AM_INTRO.start_time();
    bool WM= (*original).white_move;
    uint64_t Enemy_P=0;
    uint64_t Own_P=0;
    
    for(int i=0;i<6;i++)
    {
        Own_P |= (*original).Board[i+6*!WM];
        Enemy_P |= (*original).Board[i+6*WM];
    }
    uint64_t occupancy=Own_P | Enemy_P;
    if(extensive_time_display)
    AM_INTRO.end_time();

    int GI=0;// GOAL_INDEX
    if(extensive_time_display && knight_moves && (*original).Board[2+6*!WM])
    AM_KNIGHT.start_time();

    
    if(knight_moves)
    while(Own_Knights)
    {
        int i=find_and_delete_trailling_1(Own_Knights);
        uint64_t move_to_able_sq_knight=Kn_template[i] & ~Own_P;
        while(move_to_able_sq_knight)
        {
            int j=find_and_delete_trailling_1(move_to_able_sq_knight);
            Base_BB(original,wfh+GI);
            wfh[GI].Board[2+6*!WM] &= ~(1Ull << i);
            clear_sq_of_enemy(wfh[GI].Board,j,WM);
            wfh[GI].Board[2+6*!WM] |= 1Ull << j;
            if(!in_check(wfh[GI].Board,WM))   
            {
                castling_right_rook_correction_for_col(wfh+GI,!WM);
                GI++;
                if(GI>len_wfh)
                return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
            }

        }
    }
    if(extensive_time_display && knight_moves && (*original).Board[2+6*!WM])
    AM_KNIGHT.end_time();

    if(extensive_time_display && king_moves)
    AM_KING.start_time();

    if(king_moves)
    {
        int i=find_and_delete_trailling_1(Own_King);
        uint64_t move_to_able_sq_king=K_template[i] & ~Own_P;
        while(move_to_able_sq_king)
        {
            int j=find_and_delete_trailling_1(move_to_able_sq_king);
            Base_BB(original,wfh+GI);
            wfh[GI].Board[5+6*!WM] &= ~(1Ull << i);
            clear_sq_of_enemy(wfh[GI].Board,j,WM);
            wfh[GI].Board[5+6*!WM] |= 1Ull << j;
            if(!in_check(wfh[GI].Board,WM))   
            {
                castling_right_rook_correction_for_col(wfh+GI,!WM);
                wfh[GI].castle[WM][0]=0;
                wfh[GI].castle[WM][1]=0;
                GI++;
                if(GI>len_wfh)
                return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                
            }
            
        }
    }
    if(extensive_time_display && king_moves)
    AM_KING.end_time();

    if(extensive_time_display && pawn_capture_moves && (*original).Board[0+6*!WM])
    AM_PAWN_CAPTURE.start_time();

    if(pawn_capture_moves)
    while(Own_Pawns)
    {
        int i=find_and_delete_trailling_1(Own_Pawns);            
        if(WM)
        {
            uint64_t move_to_able_sq_pawn=BP_template[i] & (Enemy_P | original->en_passant);
            while(move_to_able_sq_pawn)
            {
                int j=find_and_delete_trailling_1(move_to_able_sq_pawn);
                Base_BB(original,wfh+GI);
                wfh[GI].Board[0] &= ~(1Ull << i);
                //wfh[GI].Board[6] &= ~(*original).en_passant;//this was an error i think
                clear_sq_of_enemy(wfh[GI].Board,j,WM);
                wfh[GI].Board[6] &= ~((original->en_passant & 1ULL << j) >> 8);
                wfh[GI].Board[0] |= 1Ull << j;
                if(!in_check(wfh[GI].Board,WM))   
                {
                    castling_right_rook_correction_for_col(wfh+GI,!WM);
                    if(j>=8*7)
                    {
                        wfh[GI].Board[0] &= ~(1Ull << j);
                                // original case =rook
                        copy_BB(wfh+GI,wfh+GI+1);
                        wfh[GI+1].Board[2] |= 1Ull << j; //  =knight
                        copy_BB(wfh+GI,wfh+GI+2);
                        wfh[GI+2].Board[3] |= 1Ull << j; //  =bishop
                        copy_BB(wfh+GI,wfh+GI+3);
                        wfh[GI+3].Board[4] |= 1Ull << j; //  =queen

                        wfh[GI].Board[1] |= 1Ull << j; //rook last piece, so it is not overwritten
                        GI=GI+3;
                    }
                
                    GI++;
                    if(GI>len_wfh)
                    return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                }            
            }       
        }
        if(!WM)
        {
            uint64_t move_to_able_sq_pawn=WP_template[i] & (Enemy_P | (*original).en_passant);
            while(move_to_able_sq_pawn)
            {
                int j=find_and_delete_trailling_1(move_to_able_sq_pawn);
                Base_BB(original,wfh+GI);
                wfh[GI].Board[6] &= ~(1Ull << i);
                //wfh[GI].Board[0] &= ~(*original).en_passant;
                wfh[GI].Board[0] &= ~((original->en_passant & 1ULL << j) << 8);
                clear_sq_of_enemy(wfh[GI].Board,j,WM);
                wfh[GI].Board[6] |= 1Ull << j;
                
                if(!in_check(wfh[GI].Board,WM))   //  oooooooooooooooooo
                {
                    castling_right_rook_correction_for_col(wfh+GI,!WM);
                    if(j<8)
                    {
                        
                        wfh[GI].Board[0+6] &= ~(1Ull << j);
                        
                                // original case =rook
                        copy_BB(wfh+GI,wfh+GI+1);
                        wfh[GI+1].Board[2+6] |= 1Ull << j; //  =knight
                        copy_BB(wfh+GI,wfh+GI+2);
                        wfh[GI+2].Board[3+6] |= 1Ull << j; //  =bishop
                        copy_BB(wfh+GI,wfh+GI+3);
                        wfh[GI+3].Board[4+6] |= 1Ull << j; //  =queen

                        wfh[GI].Board[1+6] |= 1Ull << j; //rook last piece, so it is not overwritten
                        GI=GI+3;
                    }
                    GI++;
                    if(GI>len_wfh)
                    return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
                }

            }
        }
    }
    
    if(extensive_time_display && pawn_capture_moves && (*original).Board[0+6*!WM])
    AM_PAWN_CAPTURE.end_time();

    Own_Pawns=original->Board[0+6*!original->white_move];

    if(extensive_time_display && pawn_push_moves && (*original).Board[0+6*!WM])
    AM_PAWN_PUSH.start_time();

    if(pawn_push_moves)
    while(Own_Pawns)
    {
        int i=find_and_delete_trailling_1(Own_Pawns);

        if(WM) 
        if(!((Own_P | Enemy_P) & 1Ull << (i+8) ))
        {
            Base_BB(original,wfh+GI);
            wfh[GI].Board[0] &= ~(1Ull << i);
            wfh[GI].Board[0] |= 1Ull << (i+8);
            if (!in_check(wfh[GI].Board,WM))
            {
                if(i+8>=8*7)
                {
                    wfh[GI].Board[0] &= ~(1Ull << (i+8));
                            // original case =rook
                    copy_BB(wfh+GI,wfh+GI+1);
                    wfh[GI+1].Board[2] |= 1Ull << (i+8); //  =knight
                    copy_BB(wfh+GI,wfh+GI+2);
                    wfh[GI+2].Board[3] |= 1Ull << (i+8); //  =bishop
                    copy_BB(wfh+GI,wfh+GI+3);
                    wfh[GI+3].Board[4] |= 1Ull << (i+8); //  =queen

                    wfh[GI].Board[1] |= 1Ull << (i+8); //rook last piece, so it is not overwritten
                    GI=GI+3;         
                }
                GI++;
                if(GI>len_wfh)
                return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.    
            }
            if(i<16 && !((Own_P | Enemy_P) & 1Ull << (i+16) ))
            {
                Base_BB(original,wfh+GI);
                wfh[GI].Board[0] &= ~(1Ull << i);
                wfh[GI].Board[0] |= 1Ull << (i+16);
                if (!in_check(wfh[GI].Board,WM))
                {
                    wfh[GI].en_passant = 1Ull << (i+8);
                    GI++;
                    if(GI>len_wfh)
                    return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
                }
            }
                
        }

        

        if(!WM) 
        if(!((Own_P | Enemy_P) & 1Ull << i >> 8 ))
       {
            Base_BB(original,wfh+GI);
            wfh[GI].Board[6] &= ~(1Ull << i);
            wfh[GI].Board[6] |= 1Ull << (i-8);
            if(!in_check(wfh[GI].Board,WM))   //  oooooooooooooooooo
        {
            if(i<16)
            {
                wfh[GI].Board[0+6] &= ~(1Ull << (i-8));
                         // original case =rook
                copy_BB(wfh+GI,wfh+GI+1);
                wfh[GI+1].Board[2+6] |= 1Ull << (i-8); //  =knight
                copy_BB(wfh+GI,wfh+GI+2);
                wfh[GI+2].Board[3+6] |= 1Ull << (i-8); //  =bishop
                copy_BB(wfh+GI,wfh+GI+3);
                wfh[GI+3].Board[4+6] |= 1Ull << (i-8); //  =queen

                wfh[GI].Board[1+6] |= 1Ull << (i-8); //rook last piece, so it is not overwritten
                GI=GI+3;
            }
            GI++;
            if(GI>len_wfh)
            return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
            
        }
        if(i>=8*6 && !((Own_P | Enemy_P) & 1Ull << (i-16) ))
        {
            Base_BB(original,wfh+GI);
            wfh[GI].Board[6] &= ~(1Ull << i);
            wfh[GI].Board[6] |= 1Ull << (i-16);
            if (!in_check(wfh[GI].Board,WM))
            {
                wfh[GI].en_passant = 1Ull << (i-8);
                GI++;
                if(GI>len_wfh)
                return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                
            }
        }
            
       }

    }
    if(extensive_time_display && pawn_push_moves && (*original).Board[0+6*!WM])
    AM_PAWN_PUSH.end_time();
      
    
    if(extensive_time_display && rook_moves && Own_Rooks)
    AM_ROOK_R.start_time();

    if(rook_moves )
    while(Own_Rooks)
    {
        int i=find_and_delete_trailling_1(Own_Rooks);
        int R_or_Q= (original->Board[4+6*!WM] & 1Ull << i) ? 4+6*!WM : 1+6*!WM; 

        uint64_t psudo_rook_moves=get_rook_attacks(i,occupancy);
        psudo_rook_moves &= ~Own_P;//assures that the rook does not take its own pieces
        while(psudo_rook_moves)
        {
            int j=find_and_delete_trailling_1(psudo_rook_moves);
            Base_BB(original,wfh+GI);
            wfh[GI].Board[R_or_Q] &= ~(1Ull << i);
            clear_sq_of_enemy(wfh[GI].Board,j,WM);
            wfh[GI].Board[R_or_Q] |= 1Ull <<j;
            if(!in_check(wfh[GI].Board,WM))   
            {

                castling_right_rook_correction_for_col(wfh+GI,!WM);
                if(i==0+!WM*7*8)
                wfh[GI].castle[WM][0]=0;
                if(i==7+!WM*7*8)
                wfh[GI].castle[WM][1]=0;
                GI++;
                if(GI>len_wfh)
                return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                
            }
        }
    }   
    
    if(extensive_time_display && rook_moves && (*original).Board[1+6*!WM]|(*original).Board[4+6*!WM])
    AM_ROOK_R.end_time();


    if(extensive_time_display && bishop_moves && Own_Bishops)
    AM_BISHOP_B.start_time();
    if(bishop_moves)
    while(Own_Bishops)
    {
        int i=find_and_delete_trailling_1(Own_Bishops);
        int B_or_Q= (original->Board[4+6*!WM] & 1Ull << i) ? 4+6*!WM : 3+6*!WM;
        
        uint64_t psudo_Bishop_attacks = get_bishop_attacks(i,occupancy);
        psudo_Bishop_attacks &= ~Own_P;
        while(psudo_Bishop_attacks)
        {
            int j=find_and_delete_trailling_1(psudo_Bishop_attacks);
                Base_BB(original,wfh+GI);
                wfh[GI].Board[B_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,j,WM);
                wfh[GI].Board[B_or_Q] |= 1Ull <<j;
                if(!in_check(wfh[GI].Board,WM))   
                {
                    castling_right_rook_correction_for_col(wfh+GI,!WM);
                    GI++;
                    if(GI>len_wfh)
                    return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
                }
            }
        }
    if(extensive_time_display && bishop_moves && (*original).Board[3+6*!WM]|(*original).Board[4+6*!WM])
    AM_BISHOP_B.end_time();

   
    
    if(king_moves && !in_check((*original).Board,WM))
    {   
        if(extensive_time_display && original->castle[WM][0] && !((Enemy_P | Own_P) & 0B00001110Ull << !WM*7*8)  )
        AM_CASTLE_Q.start_time();

        if(original->castle[WM][0] && !((Enemy_P | Own_P) & 0B00001110Ull << !WM*7*8)  )
        {
            Base_BB(original,wfh+GI);
            wfh[GI].Board[5+6*!WM] &= ~(1Ull << 4+!WM*7*8);
            wfh[GI].Board[5+6*!WM] |= 1Ull << 3+!WM*7*8;
            if(!in_check(wfh[GI].Board,WM))   
            {
                wfh[GI].Board[5+6*!WM] &= ~(1Ull << 3+!WM*7*8);
                wfh[GI].Board[5+6*!WM] |= 1Ull << 2+!WM*7*8;
                wfh[GI].Board[1+6*!WM] &= ~(1Ull << 0+!WM*7*8);
                wfh[GI].Board[1+6*!WM] |= 1Ull << 3+!WM*7*8;
                wfh[GI].castle[WM][0]=0;
                wfh[GI].castle[WM][1]=0;
                if(!in_check(wfh[GI].Board,WM))   
                {
                    GI++;
                    if(GI>len_wfh)
                    return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
                }
            }  
        }
        if(extensive_time_display && (*original).castle[WM][0] && !((Enemy_P | Own_P) & 0B00001110Ull << !WM*7*8)  )
        AM_CASTLE_Q.end_time();

        if(extensive_time_display && (*original).castle[WM][1] && !((Enemy_P | Own_P) & 0B01100000Ull << !WM*7*8)  )
        AM_CASTLE_K.start_time();
        if((*original).castle[WM][1] && !((Enemy_P | Own_P) & 0B01100000Ull << !WM*7*8)  )
        {
            Base_BB(original,wfh+GI);
            wfh[GI].Board[5+6*!WM] &= ~(1Ull << 4+!WM*7*8);
            wfh[GI].Board[5+6*!WM] |= 1Ull << 5+!WM*7*8;
            if(!in_check(wfh[GI].Board,WM))   
            {
                wfh[GI].Board[5+6*!WM] &= ~(1Ull << 5+!WM*7*8);
                wfh[GI].Board[5+6*!WM] |= 1Ull << 6+!WM*7*8;
                wfh[GI].Board[1+6*!WM] &= ~(1Ull << 7+!WM*7*8);
                wfh[GI].Board[1+6*!WM] |= 1Ull << 5+!WM*7*8;
                wfh[GI].castle[WM][0]=0;
                wfh[GI].castle[WM][1]=0;
                if(!in_check(wfh[GI].Board,WM))   
                {
                    GI++;
                    if(GI>len_wfh)
                    return INT_MAX;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
                }
            }  
        }
        if(extensive_time_display && (*original).castle[WM][1] && !((Enemy_P | Own_P) & 0B01100000Ull << !WM*7*8)  )
        AM_CASTLE_K.end_time();
        
    }

    if(extensive_time_display)
    AM_OUTRO.start_time();
    //no outtro needed
    AM_OUTRO.end_time();
    return GI; //number of new moves
}
     
bool one_move(const BB* const original)
{
    BB* wfh = new BB;
    uint64_t Own_Pawns=original->Board[0+6*!original->white_move];
    uint64_t Own_Knights=original->Board[2+6*!original->white_move];
    uint64_t Own_Bishops=original->Board[3+6*!original->white_move]|original->Board[4+6*!original->white_move];//careful, there are to unify queen and bishop moves
    uint64_t Own_Rooks=original->Board[1+6*!original->white_move]|original->Board[4+6*!original->white_move];
    uint64_t Own_King=original->Board[5+6*!original->white_move];
    if(extensive_time_display)
    AM_INTRO.start_time();
    bool WM= (*original).white_move;
    uint64_t Enemy_P=0;
    uint64_t Own_P=0;
    for(int i=0;i<6;i++)
    {
        Own_P |= (*original).Board[i+6*!WM];
        Enemy_P |= (*original).Board[i+6*WM];
    }
    if(extensive_time_display)
    AM_INTRO.end_time();

    int GI=0;// GOAL_INDEX//in one move not needed, but it stary zero always, so it does not matter
    if(extensive_time_display && knight_moves && (*original).Board[2+6*!WM])
    AM_KNIGHT.start_time();

    
    if(knight_moves)
    while(Own_Knights)
    {
        int i=find_and_delete_trailling_1(Own_Knights);
        uint64_t move_to_able_sq_knight=Kn_template[i] & ~Own_P;
        while(move_to_able_sq_knight)
        {
            int j=find_and_delete_trailling_1(move_to_able_sq_knight);
            Base_BB(original,wfh);
            wfh[GI].Board[2+6*!WM] &= ~(1Ull << i);
            clear_sq_of_enemy(wfh[GI].Board,j,WM);
            wfh[GI].Board[2+6*!WM] |= 1Ull << j;
            if(!in_check(wfh[GI].Board,WM))   
            {
                return 1;
            }

        }
    }
    if(extensive_time_display && knight_moves && (*original).Board[2+6*!WM])
    AM_KNIGHT.end_time();

    if(extensive_time_display && king_moves)
    AM_KING.start_time();

    if(king_moves)
    {
        int i=find_and_delete_trailling_1(Own_King);
        uint64_t move_to_able_sq_king=K_template[i] & ~Own_P;
        while(move_to_able_sq_king)
        {
            int j=find_and_delete_trailling_1(move_to_able_sq_king);
            Base_BB(original,wfh);
            wfh[GI].Board[5+6*!WM] &= ~(1Ull << i);
            clear_sq_of_enemy(wfh[GI].Board,j,WM);
            wfh[GI].Board[5+6*!WM] |= 1Ull << j;
            if(!in_check(wfh[GI].Board,WM))   
            {
                return 1;
            }
            
        }
    }
    if(extensive_time_display && king_moves)
    AM_KING.end_time();

    if(extensive_time_display && pawn_capture_moves && (*original).Board[0+6*!WM])
    AM_PAWN_CAPTURE.start_time();

    if(pawn_capture_moves)
    while(Own_Pawns)
    {
        int i=find_and_delete_trailling_1(Own_Pawns);            
        if(WM)
        {
            uint64_t move_to_able_sq_pawn=BP_template[i] & (Enemy_P | (*original).en_passant);
            while(move_to_able_sq_pawn)
            {
                int j=find_and_delete_trailling_1(move_to_able_sq_pawn);
                Base_BB(original,wfh);
                wfh[GI].Board[0] &= ~(1Ull << i);
                wfh[GI].Board[6] &= ~(*original).en_passant;
                clear_sq_of_enemy(wfh[GI].Board,j,WM);
                wfh[GI].Board[0] |= 1Ull << j;
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }            
            }       
        }
        if(!WM)
        {
            uint64_t move_to_able_sq_pawn=WP_template[i] & (Enemy_P | (*original).en_passant);
            while(move_to_able_sq_pawn)
            {
                int j=find_and_delete_trailling_1(move_to_able_sq_pawn);
                Base_BB(original,wfh);
                wfh[GI].Board[6] &= ~(1Ull << i);
                wfh[GI].Board[0] &= ~(*original).en_passant;
                clear_sq_of_enemy(wfh[GI].Board,j,WM);
                wfh[GI].Board[6] |= 1Ull << j;
                
                if(!in_check(wfh[GI].Board,WM))   //  oooooooooooooooooo
                {
                    return 1;
                }

            }
        }
    }
    
    if(extensive_time_display && pawn_capture_moves && (*original).Board[0+6*!WM])
    AM_PAWN_CAPTURE.end_time();

    Own_Pawns=original->Board[0+6*!original->white_move];

    if(extensive_time_display && pawn_push_moves && (*original).Board[0+6*!WM])
    AM_PAWN_PUSH.start_time();

    if(pawn_push_moves)
    while(Own_Pawns)
    {
        int i=find_and_delete_trailling_1(Own_Pawns);

        if(WM) 
        if(!((Own_P | Enemy_P) & 1Ull << (i+8) ))
        {
            Base_BB(original,wfh);
            wfh[GI].Board[0] &= ~(1Ull << i);
            wfh[GI].Board[0] |= 1Ull << (i+8);
            if (!in_check(wfh[GI].Board,WM))
            {
                return 1; 
            }
            if(i<16 && !((Own_P | Enemy_P) & 1Ull << (i+16) ))
            {
                Base_BB(original,wfh);
                wfh[GI].Board[0] &= ~(1Ull << i);
                wfh[GI].Board[0] |= 1Ull << (i+16);
                if (!in_check(wfh[GI].Board,WM))
                {
                    return 1;                    
                }
            }
                
        }

        

        if(!WM) 
        if(!((Own_P | Enemy_P) & 1Ull << i >> 8 ))
       {
            Base_BB(original,wfh);
            wfh[GI].Board[6] &= ~(1Ull << i);
            wfh[GI].Board[6] |= 1Ull << (i-8);
            if(!in_check(wfh[GI].Board,WM))   //  oooooooooooooooooo
        {
            return 1;
        }
        if(i>=8*6 && !((Own_P | Enemy_P) & 1Ull << (i-16) ))
        {
            Base_BB(original,wfh);
            wfh[GI].Board[6] &= ~(1Ull << i);
            wfh[GI].Board[6] |= 1Ull << (i-16);
            if (!in_check(wfh[GI].Board,WM))
            {
                return 1;
            }
        }
            
       }

    }
    if(extensive_time_display && pawn_push_moves && (*original).Board[0+6*!WM])
    AM_PAWN_PUSH.end_time();
      
    
    if(extensive_time_display && rook_moves && Own_Rooks)
    AM_ROOK_R.start_time();

    if(rook_moves )
    while(Own_Rooks)
    {
        int i=find_and_delete_trailling_1(Own_Rooks);
        int R_or_Q= (original->Board[4+6*!WM] & 1Ull << i) ? 4+6*!WM : 1+6*!WM; 
        //i can use a switch statment here. I check, if the last (up to 7) squares are empty, if they are, i dont need to check if the last (6) square are empty
        //left
        uint64_t L_sq_rook_can_move_to = R_L_template[i] & ~(Own_P);
        for(int j=1;j<8;j++)
        {
            if(L_sq_rook_can_move_to & 1Ull << i >> j)
            {
                Base_BB(original,wfh);
                wfh[GI].Board[R_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,i-j,WM);
                wfh[GI].Board[R_or_Q] |= 1Ull <<(i-j);
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }
                if(Enemy_P & 1Ull <<(i-j) ) 
                break; 
            }
            else break;      
        }
        uint64_t R_sq_rook_can_move_to = R_R_template[i] & ~(Own_P);
        for(int j=1;j<8;j++)
        {
            if(R_sq_rook_can_move_to & 1Ull << i << j)
            {
                Base_BB(original,wfh);
                wfh[GI].Board[R_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,i+j,WM);
                wfh[GI].Board[R_or_Q] |= 1Ull <<(i+j);
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }
                if(Enemy_P & 1Ull <<(i+j) ) 
                break; 
            }
            else break;
            
        }
        uint64_t U_D_sq_rook_can_move_to = U_D_template[i] & ~(Own_P);
        for(int j=1;j<8;j++)
        {
            if(U_D_sq_rook_can_move_to & 1Ull << i << 8*j)
            {
                Base_BB(original,wfh);
                wfh[GI].Board[R_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,i+8*j,WM);
                wfh[GI].Board[R_or_Q] |= 1Ull <<( i+8*j);
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }
                if(Enemy_P & 1Ull <<( i+8*j) ) 
                break; 
            }
            else break;
            
        }
        for(int j=1;j<8;j++)
        {
            if(U_D_sq_rook_can_move_to & 1Ull << i >> 8*j)
            {
                Base_BB(original,wfh);
                wfh[GI].Board[R_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,i-8*j,WM);
                wfh[GI].Board[R_or_Q] |= 1Ull <<(i-8*j);
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }
                if(Enemy_P & 1Ull <<(i-8*j) ) 
                break; 
            }
            else break;
            
        } 
    }   
    
    if(extensive_time_display && rook_moves && (*original).Board[1+6*!WM]|(*original).Board[4+6*!WM])
    AM_ROOK_R.end_time();


    if(extensive_time_display && bishop_moves && Own_Bishops)
    AM_BISHOP_B.start_time();
    if(bishop_moves)
    while(Own_Bishops)
    {
        int i=find_and_delete_trailling_1(Own_Bishops);
        int B_or_Q= (original->Board[4+6*!WM] & 1Ull << i) ? 4+6*!WM : 3+6*!WM;

        
        uint64_t L_U_sq_bisop_can_move_to = B_Left_Up_template[i] & ~(Own_P);
        for(int j=7;i+j<64;j=j+7)
        {
            if(B_Left_Up_template[i] & 1Ull <<(i+j) & Own_P)
            break;
            if(L_U_sq_bisop_can_move_to & 1Ull <<(i+j))
            {
                
                Base_BB(original,wfh);
                wfh[GI].Board[B_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,i+j,WM);
                wfh[GI].Board[B_or_Q] |= 1Ull <<(i+j);
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }
                if(Enemy_P & 1Ull <<(i+j) ) 
                break; 
            }
            
            
        }
        uint64_t R_U_sq_bishop_can_move_to = B_Right_Up_template[i] & ~(Own_P);
        for(int j=9;i+j<64;j=j+9)
        {
            if(B_Right_Up_template[i] & 1Ull << i << j & Own_P)
            break;
            if(R_U_sq_bishop_can_move_to & 1Ull << i << j)
            {
                Base_BB(original,wfh);
                wfh[GI].Board[B_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,i+j,WM);
                wfh[GI].Board[B_or_Q] |= 1Ull <<(i+j);
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }
                if(Enemy_P & 1Ull <<(i+j) ) 
                break; 
            }
            
            
        }
        uint64_t L_D_sq_bishop_can_move_to = B_Left_Down_template[i] & ~(Own_P);
        for(int j=9;i-j>=0;j=j+9)
        {
            if(B_Left_Down_template[i] & 1Ull << i >> j & Own_P)
            break;
            if(L_D_sq_bishop_can_move_to & 1Ull << i >> j)
            {
                Base_BB(original,wfh);
                wfh[GI].Board[B_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,i-j,WM);
                wfh[GI].Board[B_or_Q] |= 1Ull <<(i-j);
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }
                if(Enemy_P & 1Ull <<(i-j) ) 
                break; 
            }
            
            
        }
        uint64_t R_D_sq_bishop_can_move_to = B_Right_Down_template[i] & ~(Own_P);
        for(int j=7;i-j>=0;j=j+7)
        {
            if(B_Right_Down_template[i] & 1Ull << i >> j & Own_P)
            break;
            if(R_D_sq_bishop_can_move_to & 1Ull << i >> j)
            {
                Base_BB(original,wfh);
                wfh[GI].Board[B_or_Q] &= ~(1Ull << i);
                clear_sq_of_enemy(wfh[GI].Board,i-j,WM);
                wfh[GI].Board[B_or_Q] |= 1Ull <<(i-j);
                if(!in_check(wfh[GI].Board,WM))   
                {
                    return 1;
                }
                if(Enemy_P & 1Ull <<(i-j) ) 
                break; 
            }
            
        }
        
    }
    if(extensive_time_display && bishop_moves && (*original).Board[3+6*!WM]|(*original).Board[4+6*!WM])
    AM_BISHOP_B.end_time();


    if(extensive_time_display)
    AM_OUTRO.start_time();
    //currently no outtro
    if(extensive_time_display)
    AM_OUTRO.end_time();
    return 0; //number of new moves
}

















#endif // MOVE_GENERATION_CPP