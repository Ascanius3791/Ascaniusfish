#ifndef CHECKS_CPP
#define CHECKS_CPP

#include"../lib/checks.hpp"

bool missing_king(uint64_t Board[12], std::string message)
{
    if(!Board[5] || !Board[5+6])
    {

        std::cout << "King is Missing\n";
        if(message!="")
        print(Board,1,message);
        else
        print(Board);
        return true;
    }
    return false;
}

bool pawn_check_0(uint64_t Board[12] ,bool white_move )
{
    int king_pos=-1;
        for(int i=0;i<64;i++)
        if(Board[!white_move*6+5]& (1Ull << i))
        king_pos=i;
        if(
            !white_move && (WP_template[king_pos] & Board[0]) !=0 ||
             white_move && (BP_template[king_pos] & Board[6]) !=0
            ) 
        return true; 
        return false;   
}

bool pawn_check_1(uint64_t Board[12] ,bool white_move) //fastest known (also well tested) pawn_check
{
    int king_pos=-1;
        for(int i=0;i<64;i++)
        if(Board[!white_move*6+5]& (1Ull << i))
        king_pos=i;
    
    if(white_move && Board[6] & (1Ull << king_pos << 7 | 1Ull << king_pos << 9) & 0B11111111Ull << (king_pos/8*8+8))
    return true;
    
    if(!white_move && Board[0] & (1Ull << king_pos >> 7 | 1Ull << king_pos >> 9) & 0B11111111Ull << (king_pos/8*8) >> 8 )
    return true;
    return false;
}

bool knight_check_0(uint64_t Board[12] ,bool white_move )
{
    bool WM=white_move;
    int king_pos=-1;
        for(int i=0;i<64;i++)
        if(Board[!white_move*6+5]& (1Ull << i))
        king_pos=i;
         if(Board[2+6*WM])
        if((Kn_template[king_pos] & Board[2+6*!WM]) !=0) 
        return true;  
    return 0;
    
}

bool knight_check_1(uint64_t Board[12] ,bool white_move )
{
     bool WM=white_move;
    int king_pos=-1;
        for(int i=0;i<64;i++)
        if(Board[!white_move*6+5]& (1Ull << i))
        king_pos=i;
         if(Board[2+6*WM])
        if((Kn_template[king_pos] & Board[2+6*!WM]) !=0) 
        return true;  
    return 0;
}

//uint64_t pawn_attacks(uint64_t Board[12], bool white_move);

bool in_check(const uint64_t Board[12] ,bool white_move)
    {
        
        bool WM=white_move;
        
        int king_pos=__builtin_ctzll(Board[!white_move*6+5]);
//knight 
//==================================================================================================================================
        if(extensive_time_display && Board[2+6*WM])
        KNIGHT_CHECK.start_time();
        if(Board[2+6*WM])
        if(
            (!white_move && ((Kn_template[king_pos] & Board[2]) !=0)) ||
            ( white_move && ((Kn_template[king_pos] & Board[8]) !=0)) ) 
            {
                if(extensive_time_display)
                KNIGHT_CHECK.end_time();
                return true;    
            }
            if(extensive_time_display && Board[2+6*WM])
            KNIGHT_CHECK.end_time();
 
//==================================================================================================================================
//pawn
        if(extensive_time_display && Board[0+6*WM])
        PAWN_CHECK.start_time();
        if(Board[0+6*WM])
        if(
            !white_move && (WP_template[king_pos] & Board[0]) !=0 ||
             white_move && (BP_template[king_pos] & Board[6]) !=0
            ) 
            {
                if(extensive_time_display)
                PAWN_CHECK.end_time();
                return true; 
            }   
            if(extensive_time_display && Board[0+6*WM])
            PAWN_CHECK.end_time();

//==================================================================================================================================                                   
//king
        if(extensive_time_display)
        KING_CHECK.start_time();
        if(
            (!white_move && (((K_template[king_pos] & Board[5]) !=0))) ||
            ( white_move && (((K_template[king_pos] & Board[11]) !=0))) ) 
            {
                if(extensive_time_display)
                KING_CHECK.end_time();
                return true;    
            }  
            if(extensive_time_display)
            KING_CHECK.end_time(); 
//==================================================================================================================================
const uint64_t occupancy=Board[0]|Board[1]|Board[2]|Board[3]|Board[4]|Board[5]|Board[6]|Board[7]|Board[8]|Board[9]|Board[10]|Board[11];

//rook
        if(extensive_time_display && Board[1+6*WM] | Board[4+6*WM])
        ROOK_CHECK.start_time();
    if(Board[1+6*WM] | Board[4+6*WM])
    {
        if(get_rook_attacks(king_pos,occupancy) & (Board[1+6*WM] | Board[4+6*WM]))
        {
            if(extensive_time_display && Board[1+6*WM] | Board[4+6*WM])
            ROOK_CHECK.end_time();
            return true;
        }  
    }
    if(extensive_time_display && Board[1+6*WM] | Board[4+6*WM])
    ROOK_CHECK.end_time();
        
//==================================================================================================================================
//Bishop 
    if(extensive_time_display && Board[3+6*WM] | Board[4+6*WM])
    BISHOP_CHECK.start_time();
    if(Board[3+6*WM] | Board[4+6*WM])    
    {
        if(get_bishop_attacks(king_pos,occupancy) & (Board[3+6*WM] | Board[4+6*WM]))
        {
            if(extensive_time_display && Board[3+6*WM] | Board[4+6*WM])
            BISHOP_CHECK.end_time();
            return true;
        }
        if(extensive_time_display && Board[3+6*WM] | Board[4+6*WM])
        BISHOP_CHECK.end_time();
    }
    
//==================================================================================================================================

        return 0;
                               
        }
  










#endif // CHECKS_CPP