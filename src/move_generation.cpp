// OWNERSHIP=Ascanius
#ifndef MOVE_GENERATION_CPP
#define MOVE_GENERATION_CPP

#include "../lib/move_generation.hpp"

std::tuple<int,std::vector<Move>> all_moves(const BB* const original, BB* const wfh , int len_wfh)// returns number of moves
{
    std::vector<Move> moves;
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

    auto is_full_return_value = std::make_tuple(INT_MAX,std::vector<Move>()); //indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
    
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

                bool is_capture = (Enemy_P & 1ULL << j) != 0;
                int OWN_PICE_INDEX = 2 + 6 * !WM;
                int from = i, to = j;
                Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                GI++;
                moves.push_back(Move(i,j));
                if(GI>len_wfh)
                return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
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
                bool is_capture = (Enemy_P & 1ULL << j) != 0;
                int OWN_PICE_INDEX = 5 + 6 * !WM;
                int from = i, to = j;
                wfh[GI].castle[WM][0]=0;
                wfh[GI].castle[WM][1]=0;
                Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                GI++;
                moves.push_back(Move(i,j));
                if(GI>len_wfh)
                return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                
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
                bool is_en_passant_capture=(original->en_passant & 1ULL << j) !=0;
                wfh[GI].Board[0] |= 1Ull << j;

                int from = i, to = j;
                bool is_capture = true; // Pawn captures are always captures

                int OWN_PICE_INDEX = 0; // Pawn index
                if(!in_check(wfh[GI].Board,WM))   
                {
                    castling_right_rook_correction_for_col(wfh+GI,!WM);
                    if(j>=8*7)
                    {
                        wfh[GI].Board[0] &= ~(1Ull << j);
                                // original case =rook
                        copy_BB(wfh+GI,wfh+GI+1);
                        wfh[GI+1].Board[2] |= 1Ull << j; //  =knight
                        Zobrist::update_zobrist_hash(*original, wfh[GI+1], OWN_PICE_INDEX, from, to, is_capture, 2+6*!WM); // Update hash for knight promotion
                        copy_BB(wfh+GI,wfh+GI+2);
                        wfh[GI+2].Board[3] |= 1Ull << j; //  =bishop
                        Zobrist::update_zobrist_hash(*original, wfh[GI+2], OWN_PICE_INDEX, from, to, is_capture, 3+6*!WM); // Update hash for bishop promotion
                        copy_BB(wfh+GI,wfh+GI+3);
                        wfh[GI+3].Board[4] |= 1Ull << j; //  =queen
                        Zobrist::update_zobrist_hash(*original, wfh[GI+3], OWN_PICE_INDEX, from, to, is_capture, 4+6*!WM); // Update hash for queen promotion

                        wfh[GI].Board[1] |= 1Ull << j; //rook last piece, so it is not overwritten
                        Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture, 1+6*!WM); // Update hash for rook promotion
                        GI=GI+3;
                        moves.push_back(Move(i,j,2,0,0));//2=knight
                        moves.push_back(Move(i,j,3,0,0));//3=bishop
                        moves.push_back(Move(i,j,4,0,0));//4=queen
                        moves.push_back(Move(i,j,1,0,0));//1=rook
                    }
                    else if(is_en_passant_capture) 
                    {
                        moves.push_back(Move(i,j,0,0,1));//en passant
                        Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture, 0, true);
                    }
                    else 
                    {
                        moves.push_back(Move(i,j));
                        Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                    }
                
                    GI++;
                    if(GI>len_wfh)
                    return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
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
                bool is_en_passant_capture=(original->en_passant & 1ULL << j) !=0;
                wfh[GI].Board[6] |= 1Ull << j;
                int from = i, to = j;
                bool is_capture = true; // Pawn captures are always captures
                int OWN_PICE_INDEX = 6; // Pawn index
                if(!in_check(wfh[GI].Board,WM))   //  oooooooooooooooooo
                {
                    castling_right_rook_correction_for_col(wfh+GI,!WM);
                    if(j<8)
                    {
                        
                        wfh[GI].Board[0+6] &= ~(1Ull << j);
                        
                                // original case =rook
                        copy_BB(wfh+GI,wfh+GI+1);
                        wfh[GI+1].Board[2+6] |= 1Ull << j; //  =knight
                        Zobrist::update_zobrist_hash(*original, wfh[GI+1], OWN_PICE_INDEX, from, to, is_capture, 2+6*!WM); // Update hash for knight promotion
                        copy_BB(wfh+GI,wfh+GI+2);
                        wfh[GI+2].Board[3+6] |= 1Ull << j; //  =bishop
                        Zobrist::update_zobrist_hash(*original, wfh[GI+2], OWN_PICE_INDEX, from, to, is_capture, 3+6*!WM); // Update hash for bishop promotion
                        copy_BB(wfh+GI,wfh+GI+3);
                        wfh[GI+3].Board[4+6] |= 1Ull << j; //  =queen
                        Zobrist::update_zobrist_hash(*original, wfh[GI+3], OWN_PICE_INDEX, from, to, is_capture, 4+6*!WM); // Update hash for queen promotion

                        wfh[GI].Board[1+6] |= 1Ull << j; //rook last piece, so it is not overwritten
                        Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture, 1+6*!WM); // Update hash for rook promotion
                        GI=GI+3;
                        moves.push_back(Move(i,j,2,0,0));//2=knight
                        moves.push_back(Move(i,j,3,0,0));//3=bishop
                        moves.push_back(Move(i,j,4,0,0));//4=queen
                        moves.push_back(Move(i,j,1,0,0));//1=rook
                    }
                    else if(is_en_passant_capture)
                    {
                        moves.push_back(Move(i,j,0,0,1));//en passant
                        Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture, 0, true);
                    }
                    else {
                        moves.push_back(Move(i,j));
                        Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                    }
                    GI++;
                    if(GI>len_wfh)
                    return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
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
                int from = i, to = i + 8;
                bool is_capture = false; // Pawn pushes are not captures
                int OWN_PICE_INDEX = 0; // Pawn index
                if(i+8>=8*7)
                {
                    wfh[GI].Board[0] &= ~(1Ull << (i+8));
                            // original case =rook
                    copy_BB(wfh+GI,wfh+GI+1);
                    wfh[GI+1].Board[2] |= 1Ull << (i+8); //  =knight
                    Zobrist::update_zobrist_hash(*original, wfh[GI+1], OWN_PICE_INDEX, from, to, is_capture, 2+6*!WM); // Update hash for knight promotion
                    copy_BB(wfh+GI,wfh+GI+2);
                    wfh[GI+2].Board[3] |= 1Ull << (i+8); //  =bishop
                    Zobrist::update_zobrist_hash(*original, wfh[GI+2], OWN_PICE_INDEX, from, to, is_capture, 3+6*!WM); // Update hash for bishop promotion
                    copy_BB(wfh+GI,wfh+GI+3);
                    wfh[GI+3].Board[4] |= 1Ull << (i+8); //  =queen
                    Zobrist::update_zobrist_hash(*original, wfh[GI+3], OWN_PICE_INDEX, from, to, is_capture, 4+6*!WM); // Update hash for queen promotion

                    wfh[GI].Board[1] |= 1Ull << (i+8); //rook last piece, so it is not overwritten
                    Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture, 1+6*!WM); // Update hash for rook promotion
                    moves.push_back(Move(i,i+8,2,0,0));//2=knight
                    moves.push_back(Move(i,i+8,3,0,0));//3=bishop
                    moves.push_back(Move(i,i+8,4,0,0));//4=queen
                    moves.push_back(Move(i,i+8,1,0,0));//1=rook
                    GI=GI+3;         
                }
                else
                {   
                    moves.push_back(Move(i,i+8));
                    Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                }
                GI++;
                if(GI>len_wfh)
                return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.    
            }
            if(i<16 && !((Own_P | Enemy_P) & 1Ull << (i+16) ))
            {
                Base_BB(original,wfh+GI);
                wfh[GI].Board[0] &= ~(1Ull << i);
                wfh[GI].Board[0] |= 1Ull << (i+16);
                if (!in_check(wfh[GI].Board,WM))
                {
                    int from = i, to = i + 16;
                    bool is_capture = false; // Pawn pushes are not captures
                    int OWN_PICE_INDEX = 0; // Pawn index
                    wfh[GI].en_passant = 1Ull << (i+8);
                    Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                    GI++;
                    moves.push_back(Move(i,i+16));
                    if(GI>len_wfh)
                    return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
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
                int from = i, to = i - 8;
                bool is_capture = false; // Pawn pushes are not captures
                int OWN_PICE_INDEX = 6; // Pawn index
                if(i<16)
                {
                    wfh[GI].Board[0+6] &= ~(1Ull << (i-8));
                            // original case =rook
                    copy_BB(wfh+GI,wfh+GI+1);
                    wfh[GI+1].Board[2+6] |= 1Ull << (i-8); //  =knight
                    Zobrist::update_zobrist_hash(*original, wfh[GI+1], OWN_PICE_INDEX, from, to, is_capture, 2+6*!WM); // Update hash for knight promotion                    
                    copy_BB(wfh+GI,wfh+GI+2);
                    wfh[GI+2].Board[3+6] |= 1Ull << (i-8); //  =bishop
                    Zobrist::update_zobrist_hash(*original, wfh[GI+2], OWN_PICE_INDEX, from, to, is_capture, 3+6*!WM); // Update hash for bishop promotion
                    copy_BB(wfh+GI,wfh+GI+3);
                    wfh[GI+3].Board[4+6] |= 1Ull << (i-8); //  =queen
                    Zobrist::update_zobrist_hash(*original, wfh[GI+3], OWN_PICE_INDEX, from, to, is_capture, 4+6*!WM); // Update hash for queen promotion

                    wfh[GI].Board[1+6] |= 1Ull << (i-8); //rook last piece, so it is not overwritten
                    Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture, 1+6*!WM); // Update hash for rook promotion
                    GI=GI+3;
                    moves.push_back(Move(i,i-8,2,0,0));//2=knight
                    moves.push_back(Move(i,i-8,3,0,0));//3=bishop
                    moves.push_back(Move(i,i-8,4,0,0));//4=queen
                    moves.push_back(Move(i,i-8,1,0,0));//1=rook
                }
                else {
                    moves.push_back(Move(i,i-8));
                    Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                }
                GI++;
                if(GI>len_wfh)
                return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                
            }
        if(i>=8*6 && !((Own_P | Enemy_P) & 1Ull << (i-16) ))
        {
            Base_BB(original,wfh+GI);
            wfh[GI].Board[6] &= ~(1Ull << i);
            wfh[GI].Board[6] |= 1Ull << (i-16);
            if (!in_check(wfh[GI].Board,WM))
            {
                int from = i, to = i - 16;
                bool is_capture = false; // Pawn pushes are not captures
                int OWN_PICE_INDEX = 6; // Pawn index
                wfh[GI].en_passant = 1Ull << (i-8);
                Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                GI++;
                moves.push_back(Move(i,i-16));
                if(GI>len_wfh)
                return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                
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
                int is_capture = (Enemy_P & 1ULL << j) != 0;
                int OWN_PICE_INDEX = R_or_Q; // Rook or Queen index
                int from = i, to = j;
                castling_right_rook_correction_for_col(wfh+GI,!WM);
                
                if(i==0+!WM*7*8)
                wfh[GI].castle[WM][0]=0;
                if(i==7+!WM*7*8)
                wfh[GI].castle[WM][1]=0;
                Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                moves.push_back(Move(i,j));
                GI++;
                if(GI>len_wfh)
                return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                
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
                    bool is_capture = (Enemy_P & 1ULL << j) != 0;
                    int OWN_PICE_INDEX = B_or_Q; // Bishop or Queen index
                    int from = i, to = j;
                    castling_right_rook_correction_for_col(wfh+GI,!WM);
                    Zobrist::update_zobrist_hash(*original, wfh[GI], OWN_PICE_INDEX, from, to, is_capture);
                    GI++;
                    moves.push_back(Move(i,j));
                    if(GI>len_wfh)
                    return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
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
                    bool Kingside_castle = false; // Queenside castle
                    Zobrist::update_zobrist_hash_castling_piece_position(*original, wfh[GI],Kingside_castle);
                    //track kings position
                    int from = 3+!WM*7*8;
                    int to = 2+!WM*7*8;
                    moves.push_back(Move(from,to));
                    GI++;
                    if(GI>len_wfh)
                    return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
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
                    bool Kingside_castle = true; // Kingside castle
                    Zobrist::update_zobrist_hash_castling_piece_position(*original, wfh[GI],Kingside_castle);
                    //track kings position
                    int from = 5+!WM*7*8;
                    int to = 6+!WM*7*8;
                    moves.push_back(Move(from,to));
                    GI++;
                    if(GI>len_wfh)
                    return is_full_return_value;//indicates that the array is full//also necessiates, that the array is one longer, that it actually needs to be.
                    
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
    auto result = std::make_tuple(GI,moves);
    //now check if all the moves have the correct zobrist hash, if not, then there is a bug in the move generation
    int original_GI = GI-moves.size();
    for(int k=0;k<GI;k++)
    {   
        //std::cout << "Move " << k << " of " << GI-1 << ": ";
        int from = moves[k].from;
        int to = moves[k].to;
        int from_row = from / 8;
        char from_col = 'a' + (from % 8);
        int to_row = to / 8;
        char to_col = 'a' + (to % 8);
        //std::cout << "Checking Zobrist hash for move " << from_col << from_row+1 << "-" << to_col << to_row+1 << " in position:\n";
        if(wfh[k].zobrist_hash == Zobrist::compute_Zobrist_Hash(wfh[k]))
        {
            continue;
        }
        else
        {
            std::cout << "Move " << k << " of " << GI-1 << ": ";
            std::cout << "Zobrist hash mismatch for move " << from_col << from_row+1 << "-" << to_col << to_row+1 << " in position:\n";
            std::cout << "Expected Zobrist hash: " << wfh[k].zobrist_hash << "\n";
            std::cout << "Actual Zobrist hash: " << Zobrist::compute_Zobrist_Hash(wfh[k]) << "\n";
            print(wfh[k].Board);
            std::cin.get();
        }
    }
    //std::cin.get();

    return result; //number of new moves
}

std::string moves_to_PGN(const BB& initial_position, const std::vector<Move>& moves)
{
    BB position = initial_position;
    BB* legal_positions = new BB[256];
    std::string pgn;
    int fullmove_number = 1;

    for(size_t ply=0; ply<moves.size(); ++ply)
    {
        const Move& move = moves[ply];
        auto generated = all_moves(&position, legal_positions, 256);
        const int number_of_moves = std::get<0>(generated);
        const std::vector<Move>& legal_moves = std::get<1>(generated);
        int matching_index = -1;

        for(int index=0; index<number_of_moves; ++index)
        {
            if(legal_moves[index] == move)
            {
                matching_index = index;
                break;
            }

            if(legal_moves[index].from == move.from &&
               legal_moves[index].to == move.to &&
               legal_moves[index].promotion_piece_type == -1 &&
               move.promotion_piece_type <= -1)
            {
                matching_index = index;
                break;
            }
        }

        if(matching_index == -1)
        {
            pgn += "[invalid PV at ply " + std::to_string(ply) +
                   " move=" + std::to_string(move.from) + "-" +
                   std::to_string(move.to) + " promotion=" +
                   std::to_string(move.promotion_piece_type) + "] ";
            break;
        }

        if(position.white_move)
            pgn += std::to_string(fullmove_number) + ". ";
        else if(pgn.empty())
            pgn += std::to_string(fullmove_number) + "... ";

        pgn += get_move(&position, legal_positions + matching_index) + " ";
        position = legal_positions[matching_index];
        if(position.white_move)
            ++fullmove_number;
    }

    delete[] legal_positions;
    return pgn;
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
                delete wfh;
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
                delete wfh;
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
                    delete wfh;
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
                    delete wfh;
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
                delete wfh;
                return 1; 
            }
            if(i<16 && !((Own_P | Enemy_P) & 1Ull << (i+16) ))
            {
                Base_BB(original,wfh);
                wfh[GI].Board[0] &= ~(1Ull << i);
                wfh[GI].Board[0] |= 1Ull << (i+16);
                if (!in_check(wfh[GI].Board,WM))
                {
                    delete wfh;
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
            delete wfh;
            return 1;
        }
        if(i>=8*6 && !((Own_P | Enemy_P) & 1Ull << (i-16) ))
        {
            Base_BB(original,wfh);
            wfh[GI].Board[6] &= ~(1Ull << i);
            wfh[GI].Board[6] |= 1Ull << (i-16);
            if (!in_check(wfh[GI].Board,WM))
            {
                delete wfh;
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
                    delete wfh;
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
                    delete wfh;
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
                    delete wfh;
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
                    delete wfh;
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
                    delete wfh;
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
                    delete wfh;
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
                    delete wfh;
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
                    delete wfh;
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


Move::Move()
{
    from=0;
    to=0;
    promotion_piece_type=-1;
    is_castling=false;
    is_en_passant=false;
}

Move::Move(int from, int to, int promotion_piece_type, bool is_castling, bool is_en_passant)
{
    this->from=from;
    this->to=to;
    this->promotion_piece_type=promotion_piece_type;
    this->is_castling=is_castling;
    this->is_en_passant=is_en_passant;
}

//define == operator for Move
bool Move::operator==(const Move& other) const
{
    return from == other.from && to == other.to && promotion_piece_type == other.promotion_piece_type;
}

void PV_Line::append(const Move move,int eval)
{
    current_lenght++;
    if(current_lenght>=MAX_PV_Lenght)
    {
        std::cout<<"PV_Line::append: current_lenght>=MAX_PV_Lenght"<<std::endl;
        exit(1);
    }
    moves[current_lenght]=move;
    this->eval=eval;
}

//now create by a PV_Line from a move and a PV_Line
PV_Line::PV_Line(const Move move,int depth, const PV_Line* const pv_line)//move is the forst move then comes the pv_lin
{
    this->depth=depth;
    moves[0]=move;
    for(int i=0;i<pv_line->current_lenght;i++)
    {
        moves[i+1]=pv_line->moves[i];
    }
    current_lenght=pv_line->current_lenght+1;
    this->eval=pv_line->eval;
}
PV_Line::PV_Line(){};
PV_Line::PV_Line(int eval)
{
    this->eval=eval;
}














#endif // MOVE_GENERATION_CPP
