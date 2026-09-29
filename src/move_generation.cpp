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
    uint64_t Own_P = original->get_pieces_of_colour(WM);
    uint64_t Enemy_P = original->get_pieces_of_colour(!WM);
    uint64_t occupancy = original->get_occupancy();
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
                        moves.push_back(Move(i,j,1,0,0));//1=rook - same order as the four boards above (rook, knight, bishop, queen)
                        moves.push_back(Move(i,j,2,0,0));//2=knight
                        moves.push_back(Move(i,j,3,0,0));//3=bishop
                        moves.push_back(Move(i,j,4,0,0));//4=queen
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
                        moves.push_back(Move(i,j,1,0,0));//1=rook - same order as the four boards above (rook, knight, bishop, queen)
                        moves.push_back(Move(i,j,2,0,0));//2=knight
                        moves.push_back(Move(i,j,3,0,0));//3=bishop
                        moves.push_back(Move(i,j,4,0,0));//4=queen
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
                    moves.push_back(Move(i,i+8,1,0,0));//1=rook - same order as the four boards above (rook, knight, bishop, queen)
                    moves.push_back(Move(i,i+8,2,0,0));//2=knight
                    moves.push_back(Move(i,i+8,3,0,0));//3=bishop
                    moves.push_back(Move(i,i+8,4,0,0));//4=queen
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
                    moves.push_back(Move(i,i-8,1,0,0));//1=rook - same order as the four boards above (rook, knight, bishop, queen)
                    moves.push_back(Move(i,i-8,2,0,0));//2=knight
                    moves.push_back(Move(i,i-8,3,0,0));//3=bishop
                    moves.push_back(Move(i,i-8,4,0,0));//4=queen
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
    if constexpr (DEBUG_MODE)
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

// ---------------------------------------------------------------------------
// generate_legal_moves() / count_legal_moves() / make_move()
// ---------------------------------------------------------------------------

// between_squares.sq[a][b]: the squares strictly between a and b when they share
// a rank, file or diagonal, else 0. Built at compile time.
struct Between_Table
{
    uint64_t sq[64][64];
};

constexpr Between_Table make_between_table()
{
    Between_Table t{};
    for(int a=0;a<64;a++)
    for(int b=0;b<64;b++)
    {
        const int ra=a/8, fa=a%8, rb=b/8, fb=b%8;
        const int dr=(rb>ra)-(rb<ra), df=(fb>fa)-(fb<fa);
        const bool aligned = a!=b && (ra==rb || fa==fb || ra-rb==fa-fb || ra-rb==fb-fa);
        uint64_t bb=0;
        if(aligned)
        for(int r=ra+dr, f=fa+df; r!=rb || f!=fb; r+=dr, f+=df)
        bb |= 1ULL << (r*8+f);
        t.sq[a][b]=bb;
    }
    return t;
}

inline constexpr Between_Table between_squares = make_between_table();

// Is sq attacked by the side not to move, with occupancy occ? Same attacks
// in_check() tests, the enemy king included.
static inline bool enemy_attacks_square(const BB* const pos, int sq, uint64_t occ)
{
    const bool WM = pos->white_move;
    const int ENE = 6*WM;
    const uint64_t* const B = pos->Board;
    return ((WM ? BP_template[sq] : WP_template[sq]) & B[0+ENE])
        || (Kn_template[sq] & B[2+ENE])
        || (K_template[sq] & B[5+ENE])
        || (get_rook_attacks(sq,occ) & (B[1+ENE]|B[4+ENE]))
        || (get_bishop_attacks(sq,occ) & (B[3+ENE]|B[4+ENE]));
}

// What decides legality at one node, computed once for all its moves.
struct Legal_Masks
{
    uint64_t own, enemy, occupancy;
    uint64_t checkmask;   // where a non-king move must land: everything, or capture/block the one checker, or nothing in double check
    uint64_t pinned;      // own pieces pinned to the king
    uint64_t pin_ray[64]; // for a pinned square: the ray it may move along, pinner included; unset elsewhere
    int king_sq;
    bool in_check;
};

static inline void compute_legal_masks(const BB* const pos, Legal_Masks& m)
{
    const bool WM = pos->white_move;
    const int OWN = 6*!WM, ENE = 6*WM;
    const uint64_t* const B = pos->Board;
    m.own = B[0+OWN]|B[1+OWN]|B[2+OWN]|B[3+OWN]|B[4+OWN]|B[5+OWN];
    m.enemy = B[0+ENE]|B[1+ENE]|B[2+ENE]|B[3+ENE]|B[4+ENE]|B[5+ENE];
    m.occupancy = m.own | m.enemy;
    const int k = __builtin_ctzll(B[5+OWN]);
    m.king_sq = k;
    const uint64_t enemy_orth = B[1+ENE]|B[4+ENE];
    const uint64_t enemy_diag = B[3+ENE]|B[4+ENE];

    uint64_t checkers = ((WM ? BP_template[k] : WP_template[k]) & B[0+ENE])
                      | (Kn_template[k] & B[2+ENE])
                      | (K_template[k] & B[5+ENE])
                      | (get_rook_attacks(k,m.occupancy) & enemy_orth)
                      | (get_bishop_attacks(k,m.occupancy) & enemy_diag);
    m.in_check = checkers != 0;
    if(!checkers)
    m.checkmask = ~0ULL;
    else if(checkers & (checkers-1))
    m.checkmask = 0;
    else
    m.checkmask = checkers | between_squares.sq[k][__builtin_ctzll(checkers)];

    // An enemy slider that sees the king through own pieces pins the one own
    // piece between them, if there is exactly one.
    m.pinned = 0;
    uint64_t pinners = (get_rook_attacks(k,m.enemy) & enemy_orth)
                     | (get_bishop_attacks(k,m.enemy) & enemy_diag);
    while(pinners)
    {
        const int p = find_and_delete_trailling_1(pinners);
        const uint64_t between = between_squares.sq[k][p];
        const uint64_t blockers = between & m.own;
        if(blockers && !(blockers & (blockers-1)))
        {
            const int b = __builtin_ctzll(blockers);
            m.pinned |= blockers;
            m.pin_ray[b] = between | 1ULL << p;
        }
    }
}

// En passant, tested the way all_moves() tests it: make it and ask in_check().
static inline bool en_passant_is_legal(const BB* const pos, int from, int to)
{
    const bool WM = pos->white_move;
    uint64_t b[12];
    for(int i=0;i<12;i++)
    b[i]=pos->Board[i];
    const int OWN = 6*!WM, ENE = 6*WM;
    b[0+OWN] &= ~(1ULL << from);
    b[0+OWN] |= 1ULL << to;
    b[0+ENE] &= ~(WM ? 1ULL << to >> 8 : 1ULL << to << 8);
    for(int i=0;i<6;i++)
    b[i+ENE] &= ~(1ULL << to);
    return !in_check(b,WM);
}

template<Gen_Mode MODE>
int generate_legal_moves(const BB* const pos, Move_List& list)
{
    Legal_Masks m;
    compute_legal_masks(pos,m);
    const bool WM = pos->white_move;
    const int OWN = 6*!WM;
    const uint64_t* const B = pos->Board;
    Move* const first = list.moves + list.size;
    Move* out = first;

    // where a piece may go by mode, before legality
    const uint64_t mode_targets = MODE==GEN_ALL ? ~m.own : MODE==GEN_CAPTURES ? m.enemy : ~m.occupancy;
    const uint64_t legal_targets = mode_targets & m.checkmask;

    // knights (a pinned knight never moves)
    uint64_t knights = B[2+OWN] & ~m.pinned;
    while(knights)
    {
        const int i = find_and_delete_trailling_1(knights);
        uint64_t t = Kn_template[i] & legal_targets;
        while(t)
        *out++ = Move(i,find_and_delete_trailling_1(t));
    }

    // king
    {
        const int k = m.king_sq;
        const uint64_t occ_no_king = m.occupancy & ~(1ULL << k);
        uint64_t t = K_template[k] & mode_targets;
        while(t)
        {
            const int j = find_and_delete_trailling_1(t);
            if(!enemy_attacks_square(pos,j,occ_no_king))
            *out++ = Move(k,j);
        }
    }

    const uint64_t pawns = B[0+OWN];
    const uint64_t last_rank = WM ? 0xFF00000000000000ULL : 0xFFULL;

    // pawn captures, en passant and capture-promotions included
    if(MODE!=GEN_QUIETS)
    {
        uint64_t p = pawns;
        while(p)
        {
            const int i = find_and_delete_trailling_1(p);
            const uint64_t allowed = m.checkmask & (m.pinned >> i & 1 ? m.pin_ray[i] : ~0ULL);
            uint64_t t = (WM ? BP_template[i] : WP_template[i]) & (m.enemy | pos->en_passant);
            while(t)
            {
                const int j = find_and_delete_trailling_1(t);
                const uint64_t to = 1ULL << j;
                if(pos->en_passant & to)
                {
                    if(en_passant_is_legal(pos,i,j))
                    *out++ = Move(i,j,0,0,1);
                }
                else if(allowed & to)
                {
                    if(to & last_rank)
                    {
                        *out++ = Move(i,j,1,0,0);
                        *out++ = Move(i,j,2,0,0);
                        *out++ = Move(i,j,3,0,0);
                        *out++ = Move(i,j,4,0,0);
                    }
                    else
                    *out++ = Move(i,j);
                }
            }
        }
    }

    // pawn pushes: per pawn the single push, then the double push
    {
        uint64_t p = pawns;
        while(p)
        {
            const int i = find_and_delete_trailling_1(p);
            const int s = WM ? i+8 : i-8;
            if(m.occupancy >> s & 1)
            continue;
            const uint64_t allowed = m.checkmask & (m.pinned >> i & 1 ? m.pin_ray[i] : ~0ULL);
            if(allowed >> s & 1)
            {
                if(last_rank >> s & 1)
                {
                    if(MODE!=GEN_QUIETS)
                    {
                        *out++ = Move(i,s,1,0,0);
                        *out++ = Move(i,s,2,0,0);
                        *out++ = Move(i,s,3,0,0);
                        *out++ = Move(i,s,4,0,0);
                    }
                }
                else if(MODE!=GEN_CAPTURES)
                *out++ = Move(i,s);
            }
            if(MODE!=GEN_CAPTURES && (WM ? i<16 : i>=48))
            {
                const int d = WM ? i+16 : i-16;
                if(!(m.occupancy >> d & 1) && (allowed >> d & 1))
                *out++ = Move(i,d);
            }
        }
    }

    // rooks and queens along ranks and files
    uint64_t orth = B[1+OWN] | B[4+OWN];
    while(orth)
    {
        const int i = find_and_delete_trailling_1(orth);
        uint64_t t = get_rook_attacks(i,m.occupancy) & legal_targets;
        if(m.pinned >> i & 1)
        t &= m.pin_ray[i];
        while(t)
        *out++ = Move(i,find_and_delete_trailling_1(t));
    }

    // bishops and queens along diagonals
    uint64_t diag = B[3+OWN] | B[4+OWN];
    while(diag)
    {
        const int i = find_and_delete_trailling_1(diag);
        uint64_t t = get_bishop_attacks(i,m.occupancy) & legal_targets;
        if(m.pinned >> i & 1)
        t &= m.pin_ray[i];
        while(t)
        *out++ = Move(i,find_and_delete_trailling_1(t));
    }

    // castling, encoded as all_moves() does: the king's first step's square to
    // its last (d1-c1, f1-g1), with is_castling left false
    if(MODE!=GEN_CAPTURES && !m.in_check)
    {
        const int base = WM ? 0 : 56;
        if(pos->castle[WM][0] && !(m.occupancy & 0B00001110ULL << base)
           && !enemy_attacks_square(pos,base+3,m.occupancy) && !enemy_attacks_square(pos,base+2,m.occupancy))
        *out++ = Move(base+3,base+2);
        if(pos->castle[WM][1] && !(m.occupancy & 0B01100000ULL << base)
           && !enemy_attacks_square(pos,base+5,m.occupancy) && !enemy_attacks_square(pos,base+6,m.occupancy))
        *out++ = Move(base+5,base+6);
    }

    const int added = out - first;
    list.size += added;
    return added;
}

template int generate_legal_moves<GEN_ALL>(const BB* const, Move_List&);
template int generate_legal_moves<GEN_CAPTURES>(const BB* const, Move_List&);
template int generate_legal_moves<GEN_QUIETS>(const BB* const, Move_List&);

int generate_legal_moves(const BB* const pos, Move_List& list, Gen_Mode mode)
{
    switch(mode)
    {
        case GEN_CAPTURES: return generate_legal_moves<GEN_CAPTURES>(pos,list);
        case GEN_QUIETS:   return generate_legal_moves<GEN_QUIETS>(pos,list);
        default:           return generate_legal_moves<GEN_ALL>(pos,list);
    }
}

int count_legal_moves(const BB* const pos, int limit)
{
    Legal_Masks m;
    compute_legal_masks(pos,m);
    const bool WM = pos->white_move;
    const int OWN = 6*!WM;
    const uint64_t* const B = pos->Board;
    int n = 0;

    // king first: in double check it is all there is
    {
        const int k = m.king_sq;
        const uint64_t occ_no_king = m.occupancy & ~(1ULL << k);
        uint64_t t = K_template[k] & ~m.own;
        while(t)
        {
            const int j = find_and_delete_trailling_1(t);
            if(!enemy_attacks_square(pos,j,occ_no_king) && ++n>=limit)
            return limit;
        }
    }

    const uint64_t legal_targets = ~m.own & m.checkmask;
    const uint64_t last_rank = WM ? 0xFF00000000000000ULL : 0xFFULL;

    // pawns (en passant is tested even in double check, as all_moves() does)
    uint64_t p = B[0+OWN];
    while(p)
    {
        const int i = find_and_delete_trailling_1(p);
        const uint64_t allowed = m.checkmask & (m.pinned >> i & 1 ? m.pin_ray[i] : ~0ULL);
        const uint64_t attacks = WM ? BP_template[i] : WP_template[i];
        const uint64_t caps = attacks & m.enemy & allowed;
        n += __builtin_popcountll(caps & ~last_rank) + 4*__builtin_popcountll(caps & last_rank);
        if(attacks & pos->en_passant)
        n += en_passant_is_legal(pos,i,__builtin_ctzll(pos->en_passant));
        const int s = WM ? i+8 : i-8;
        if(!(m.occupancy >> s & 1))
        {
            if(allowed >> s & 1)
            n += last_rank >> s & 1 ? 4 : 1;
            if(WM ? i<16 : i>=48)
            {
                const int d = WM ? i+16 : i-16;
                n += !(m.occupancy >> d & 1) && (allowed >> d & 1);
            }
        }
        if(n>=limit)
        return limit;
    }
    if(!m.checkmask)
    return n;

    uint64_t knights = B[2+OWN] & ~m.pinned;
    while(knights)
    n += __builtin_popcountll(Kn_template[find_and_delete_trailling_1(knights)] & legal_targets);
    if(n>=limit)
    return limit;

    uint64_t orth = B[1+OWN] | B[4+OWN];
    while(orth)
    {
        const int i = find_and_delete_trailling_1(orth);
        uint64_t t = get_rook_attacks(i,m.occupancy) & legal_targets;
        if(m.pinned >> i & 1)
        t &= m.pin_ray[i];
        n += __builtin_popcountll(t);
    }
    uint64_t diag = B[3+OWN] | B[4+OWN];
    while(diag)
    {
        const int i = find_and_delete_trailling_1(diag);
        uint64_t t = get_bishop_attacks(i,m.occupancy) & legal_targets;
        if(m.pinned >> i & 1)
        t &= m.pin_ray[i];
        n += __builtin_popcountll(t);
    }
    if(n>=limit)
    return limit;

    if(!m.in_check)
    {
        const int base = WM ? 0 : 56;
        n += pos->castle[WM][0] && !(m.occupancy & 0B00001110ULL << base)
             && !enemy_attacks_square(pos,base+3,m.occupancy) && !enemy_attacks_square(pos,base+2,m.occupancy);
        n += pos->castle[WM][1] && !(m.occupancy & 0B01100000ULL << base)
             && !enemy_attacks_square(pos,base+5,m.occupancy) && !enemy_attacks_square(pos,base+6,m.occupancy);
    }
    return n<limit ? n : limit;
}

static inline int castling_key_index(const BB* const pos)
{
    return pos->castle[1][1] << 3 | pos->castle[1][0] << 2 | pos->castle[0][1] << 1 | pos->castle[0][0];
}

// An empty cache to copy from. Base_BB()'s lazy.reset() builds a temporary and
// costs ~15 ns, most of a make_move(); copying this one costs ~3.
static const BB_lazyfields empty_lazyfields;

void make_move(const BB* const parent, const Move& move, BB* const child)
{
    // Base_BB(parent,child), with the cache cleared by copy
    for(int i=0;i<12;i++)
    child->Board[i]=parent->Board[i];
    child->white_move=!parent->white_move;
    child->castle[0][0]=parent->castle[0][0];
    child->castle[0][1]=parent->castle[0][1];
    child->castle[1][0]=parent->castle[1][0];
    child->castle[1][1]=parent->castle[1][1];
    child->en_passant=0;
    child->number_of_repetitions=parent->number_of_repetitions;
    child->move=parent->move+1;
    child->halfmoves_since_last_capture_or_pawn_move=parent->halfmoves_since_last_capture_or_pawn_move+1;
    child->lazy=empty_lazyfields;

    const bool WM = parent->white_move;
    const int OWN = 6*!WM, ENE = 6*WM;
    const int from = move.from, to = move.to;
    const uint64_t from_bb = 1ULL << from, to_bb = 1ULL << to;
    uint64_t* const B = child->Board;
    uint64_t hash = parent->zobrist_hash;

    int piece = -1;
    for(int i=0;i<6;i++)
    if(parent->Board[i+OWN] & from_bb)
    {
        piece = i+OWN;
        break;
    }

    if(piece<0)
    {
        // castling: nothing of ours stands on `from`, the square the king crosses
        const int base = WM ? 0 : 56;
        const bool king_side = to==base+6;
        const int king_to = to;
        const int rook_from = king_side ? base+7 : base;
        const int rook_to = from;
        B[5+OWN] &= ~(1ULL << (base+4));
        B[5+OWN] |= 1ULL << king_to;
        B[1+OWN] &= ~(1ULL << rook_from);
        B[1+OWN] |= 1ULL << rook_to;
        child->castle[WM][0]=0;
        child->castle[WM][1]=0;
        hash ^= Zobrist::pieceKeys[5+OWN][base+4] ^ Zobrist::pieceKeys[5+OWN][king_to];
        hash ^= Zobrist::pieceKeys[1+OWN][rook_from] ^ Zobrist::pieceKeys[1+OWN][rook_to];
    }
    else
    {
        const int type = piece-OWN;
        const bool pawn_push = type==0 && from%8==to%8;
        const bool en_passant = type==0 && !pawn_push && (parent->en_passant & to_bb);

        B[piece] &= ~from_bb;
        hash ^= Zobrist::pieceKeys[piece][from];
        if(en_passant)
        {
            const int captured_sq = WM ? to-8 : to+8;
            B[0+ENE] &= ~(1ULL << captured_sq);
            hash ^= Zobrist::pieceKeys[0+ENE][captured_sq];
        }
        else
        for(int i=0;i<6;i++)
        if(parent->Board[i+ENE] & to_bb)
        {
            hash ^= Zobrist::pieceKeys[i+ENE][to];
            break;
        }
        clear_sq_of_enemy(B,to,WM);

        if(type==0 && (WM ? to>=56 : to<8))
        {
            const int promoted = move.promotion_piece_type+OWN;
            B[promoted] |= to_bb;
            hash ^= Zobrist::pieceKeys[promoted][to];
        }
        else
        {
            B[piece] |= to_bb;
            hash ^= Zobrist::pieceKeys[piece][to];
        }

        if(!pawn_push)
        castling_right_rook_correction_for_col(child,!WM);
        if(type==5)
        {
            child->castle[WM][0]=0;
            child->castle[WM][1]=0;
        }
        // a rook, or a queen moving like one, leaving a corner (all_moves()'s rook loop)
        if(type==1 || (type==4 && (from/8==to/8 || from%8==to%8)))
        {
            const int base = WM ? 0 : 56;
            if(from==base)
            child->castle[WM][0]=0;
            if(from==base+7)
            child->castle[WM][1]=0;
        }
        if(pawn_push && (to-from==16 || from-to==16))
        child->en_passant = 1ULL << ((from+to)/2);
    }

    hash ^= Zobrist::castlingKeys[castling_key_index(parent)] ^ Zobrist::castlingKeys[castling_key_index(child)];
    if(parent->en_passant)
    hash ^= Zobrist::enPassantKeys[__builtin_ctzll(parent->en_passant)%8];
    if(child->en_passant)
    hash ^= Zobrist::enPassantKeys[__builtin_ctzll(child->en_passant)%8];
    hash ^= Zobrist::sideKey;
    child->zobrist_hash = hash;
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

Chunk_pool<PV_extension, PV_POOL_CAP>& pv_extension_pool()
{
    static Chunk_pool<PV_extension, PV_POOL_CAP> pool;
    return pool;
}

//returned for any read past current_lenght, so at() can never go out of bounds
static const Move pv_line_no_move;

const Move& PV_Line::at(int i) const
{
    if(i<0 || i>=current_lenght)
    return pv_line_no_move;
    if(i<PV_CHUNK)
    return moves[i];
    const PV_extension* chunk = extension;
    const int block = i/PV_CHUNK - 1;//block 0 is the first extension chunk
    for(int b=0;b<block && chunk;b++)
    chunk = chunk->next;
    if(!chunk)
    return pv_line_no_move;//pool ran dry while this line was being built
    return chunk->moves[i%PV_CHUNK];
}

bool PV_Line::set_move(int i, const Move& move)
{
    if(i<0 || i>=MAX_PV_Lenght)
    return false;
    if(i<PV_CHUNK)
    {
        moves[i]=move;
        return true;
    }
    const int block = i/PV_CHUNK - 1;
    PV_extension** slot = &extension;
    for(int b=0;b<=block;b++)
    {
        if(!*slot)
        {
            PV_extension* chunk = pv_extension_pool().acquire();
            if(!chunk)
            {
                truncated = true;//pool empty: caller keeps what already fits
                return false;
            }
            *slot = chunk;
        }
        if(b==block)
        {
            (*slot)->moves[i%PV_CHUNK]=move;
            return true;
        }
        slot = &(*slot)->next;
    }
    return false;
}

std::vector<Move> PV_Line::first_n(int n) const
{
    if(n>current_lenght)
    n=current_lenght;
    std::vector<Move> out;
    if(n<=0)
    return out;
    out.reserve(n);
    for(int i=0;i<n;i++)
    out.push_back(at(i));
    return out;
}

void PV_Line::clear_extension()
{
    if(extension)
    {
        pv_extension_pool().release_chain(extension);
        extension=nullptr;
    }
}

//Deep-copies other's moves. Chunks are cloned a whole chunk at a time rather
//than move by move, so a long line costs one acquire() per 8 moves instead of a
//chain walk per move. If the pool runs dry the copy keeps the moves that fit and
//current_lenght is pulled back to match, so it never reports moves it lacks.
void PV_Line::clone_moves_from(const PV_Line& other)
{
    const int inline_moves = other.current_lenght<PV_CHUNK ? other.current_lenght : PV_CHUNK;
    for(int i=0;i<inline_moves;i++)
    moves[i]=other.moves[i];
    current_lenght=other.current_lenght;
    truncated=other.truncated;
    extension=nullptr;

    const PV_extension* src = other.extension;
    PV_extension** dst = &extension;
    int stored = PV_CHUNK;
    while(src)
    {
        PV_extension* chunk = pv_extension_pool().acquire();
        if(!chunk)
        {
            truncated = true;
            if(current_lenght>stored)
            current_lenght = stored;
            return;
        }
        for(int k=0;k<PV_CHUNK;k++)
        chunk->moves[k]=src->moves[k];
        chunk->next=nullptr;
        *dst=chunk;
        dst=&chunk->next;
        stored+=PV_CHUNK;
        src=src->next;
    }
}

//Appends one move at the end of the line. NOTE: the previous version
//incremented current_lenght BEFORE writing, so the first append landed at
//moves[1] and moves[0] stayed unset; it had no callers, so this writes at
//current_lenght instead, which is what the name says.
void PV_Line::append(const Move move,int eval)
{
    if(current_lenght>=MAX_PV_Lenght)
    {
        std::cout<<"PV_Line::append: current_lenght>=MAX_PV_Lenght"<<std::endl;
        exit(1);
    }
    if(set_move(current_lenght,move))
    current_lenght++;
    this->eval=eval;
}

//now create by a PV_Line from a move and a PV_Line
PV_Line::PV_Line(const Move move,int depth, const PV_Line* const pv_line)//move is the forst move then comes the pv_lin
{
    this->depth=depth;
    moves[0]=move;
    current_lenght=1;
    if(!pv_line)
    return;
    this->eval=pv_line->eval;
    truncated=pv_line->truncated;
    const int n=pv_line->current_lenght;
    for(int i=0;i<n;i++)
    {
        if(!set_move(i+1,pv_line->at(i)))
        break;//pool empty: keep the prefix, truncated is already set
        current_lenght=i+2;
    }
}
PV_Line::PV_Line(){};
PV_Line::PV_Line(int eval)
{
    this->eval=eval;
}

PV_Line::PV_Line(const PV_Line& other)
{
    depth=other.depth;
    eval=other.eval;
    bound_type=other.bound_type;
    clone_moves_from(other);
}

PV_Line::PV_Line(PV_Line&& other) noexcept
{
    depth=other.depth;
    eval=other.eval;
    bound_type=other.bound_type;
    current_lenght=other.current_lenght;
    truncated=other.truncated;
    const int inline_moves = other.current_lenght<PV_CHUNK ? other.current_lenght : PV_CHUNK;
    for(int i=0;i<inline_moves;i++)
    moves[i]=other.moves[i];
    extension=other.extension;//stolen, no chunk changes hands
    other.extension=nullptr;
    other.current_lenght=0;
}

PV_Line& PV_Line::operator=(const PV_Line& other)
{
    if(this==&other)
    return *this;
    clear_extension();//our old chain goes back before we take on another
    depth=other.depth;
    eval=other.eval;
    bound_type=other.bound_type;
    clone_moves_from(other);
    return *this;
}

PV_Line& PV_Line::operator=(PV_Line&& other) noexcept
{
    if(this==&other)
    return *this;
    clear_extension();
    depth=other.depth;
    eval=other.eval;
    bound_type=other.bound_type;
    current_lenght=other.current_lenght;
    truncated=other.truncated;
    const int inline_moves = other.current_lenght<PV_CHUNK ? other.current_lenght : PV_CHUNK;
    for(int i=0;i<inline_moves;i++)
    moves[i]=other.moves[i];
    extension=other.extension;
    other.extension=nullptr;
    other.current_lenght=0;
    return *this;
}

PV_Line::~PV_Line()
{
    clear_extension();
}














#endif // MOVE_GENERATION_CPP
