#ifndef BITBOARD_INITIALISATIONS_CPP
#define BITBOARD_INITIALISATIONS_CPP
#include"../lib/Bitboard_initialisations.hpp"



void initialize_FEN_to::translate( const bool original[8][8][2][6] , uint64_t goal[12])
{
    initialize_FEN_to::empty(goal);
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    for(int col=0;col<2;col++)
    for(int piece=0;piece<6;piece++)
    if(original[i][j][col][piece])
    goal[piece+col*6]=goal[piece+col*6] | (1Ull << (j+i*8));
}

void initialize_FEN_to::translate(const uint64_t original[12], bool goal[8][8][2][6])
{
    initialize_FEN_to::empty(goal);
    for(int i=0;i<8;i++)
    for(int j=0;j<8;j++)
    for(int col=0;col<2;col++)
    for(int piece=0;piece<6;piece++)
    if(original[piece+6*col] & 1Ull << (8*i+j))
    goal[i][j][col][piece]=true;
}

void initialize_FEN_to::empty(bool FEN[8][8][2][6] )
{
    for(int i=0;i<8;i++) 
    for(int j=0;j<8;j++)
    for(int col=0;col<2;col++)
    for(int piece=0;piece<6;piece++)
    FEN[i][j][col][piece]=false;
}

void initialize_FEN_to::empty(uint64_t Board[12])
{
    for(int i=0;i<12;i++)
    Board[i]=0;
}

void initialize_FEN_to::Standartboard(uint64_t Board[12])
{
    //W     //  1000000110000001100000011000000110000001100000011000000110000001
            //  10000001 10000001 10000001 10000001 10000001 10000001 10000001 10000001
    Board[0] =0B0000000000000000000000000000000000000000000000001111111100000000;//P
    Board[1] =0B0000000000000000000000000000000000000000000000000000000010000001;//R
    Board[2] =0B0000000000000000000000000000000000000000000000000000000001000010;//Kn
    Board[3] =0B0000000000000000000000000000000000000000000000000000000000100100;//B
    Board[4] =0B0000000000000000000000000000000000000000000000000000000000001000;//Q
    Board[5] =0B0000000000000000000000000000000000000000000000000000000000010000;//K
    //B
    Board[6] =0B0000000011111111000000000000000000000000000000000000000000000000;//P
    Board[7] =0B1000000100000000000000000000000000000000000000000000000000000000;//R
    Board[8] =0B0100001000000000000000000000000000000000000000000000000000000000;//Kn
    Board[9] =0B0010010000000000000000000000000000000000000000000000000000000000;//B
    Board[10]=0B0000100000000000000000000000000000000000000000000000000000000000;//Q
    Board[11]=0B0001000000000000000000000000000000000000000000000000000000000000;//K



}

void initialize_FEN_to::Standartboard(bool FEN[8][8][2][6] )
{   
    uint64_t Board[12];
    Standartboard(Board);
    translate(Board,FEN);
}

void initialize_FEN_to::Position_1(uint64_t Board[12])
{
    empty(Board);
    Board[5]=1Ull << 23;
    Board[11]=1Ull << 24;
    Board[10]=1Ull << 5*8+4;
}

void initialize_FEN_to::puzzle_1(bool FEN[8][8][2][6] )
{
    empty(FEN);

    FEN[2][0][0][0]=true;
    FEN[1][1][0][0]=true;
    FEN[1][2][0][0]=true;
    FEN[1][5][0][0]=true;
    FEN[5][5][0][0]=true;
    FEN[1][6][0][0]=true;
    FEN[1][7][0][0]=true;
    

    FEN[6][0][1][0]=true;
    FEN[6][1][1][0]=true;
    FEN[4][3][1][0]=true;
    FEN[5][4][1][0]=true;
    FEN[6][6][1][0]=true;
    FEN[6][7][1][0]=true;

    FEN[2][3][0][3]=true;
    FEN[2][7][0][4]=true;
    FEN[0][4][0][1]=true;
    FEN[0][6][0][5]=true;

    FEN[7][0][1][1]=true;
    FEN[7][2][1][3]=true;
    FEN[7][5][1][1]=true;
    FEN[7][6][1][5]=true;
    FEN[6][3][1][2]=true;
    FEN[4][2][1][3]=true;
    FEN[3][5][1][4]=true;

}

void initialize_FEN_to::puzzle_1(uint64_t Board[12] )
{
    bool FEN[8][8][2][6];
    puzzle_1(FEN);
    translate(FEN,Board);
}

void initialize_FEN_to::puzzle_2(bool FEN[8][8][2][6] )
{
    empty(FEN);
    
    FEN[1][1][0][0]=true;
    FEN[2][1][1][0]=true;

    FEN[5][0][0][1]=true;

    FEN[6][4][0][5]=true;
    FEN[6][5][0][3]=true;
    FEN[5][6][0][2]=true;
    FEN[6][6][1][0]=true;
    FEN[6][7][1][5]=true;
    FEN[5][7][1][0]=true;
    FEN[4][7][1][0]=true;
    FEN[3][7][0][0]=true;
    FEN[3][7][0][0]=true;


}

void initialize_FEN_to::puzzle_2(uint64_t Board[12] )
{
    bool FEN[8][8][2][6];
    initialize_FEN_to::puzzle_2(FEN);
    translate(FEN,Board);
}

void initialize_FEN_to::puzzle_3(bool FEN[8][8][2][6] )
{
    empty(FEN); 
    
    FEN[0][0][1][5]=true;
    FEN[0][2][0][5]=true;
    FEN[0][3][0][3]=true;

    FEN[1][0][1][0]=true;
    FEN[1][2][0][0]=true;

    FEN[6][4][0][3]=true;
    FEN[6][5][1][1]=true;
    


}

void initialize_FEN_to::puzzle_3(uint64_t Board[12] )
{
    bool FEN[8][8][2][6];
    puzzle_3(FEN);
    translate(FEN,Board);
}

void initialize_FEN_to::puzzle_4(bool FEN[8][8][2][6] )
{
    empty(FEN);
    
    FEN[7][7][1][5]=true;
    FEN[6][7][1][0]=true;
    FEN[6][6][1][0]=true;

    FEN[6][5][1][1]=true;
    FEN[0][4][0][1]=true;

    FEN[1][6][0][5]=true;
}

void initialize_FEN_to::puzzle_4(uint64_t Board[12] )
{
    bool FEN[8][8][2][6];
    puzzle_4(FEN);
    translate(FEN,Board);
}

void initialize_FEN_to::puzzle_5(bool FEN[8][8][2][6])
{
    empty(FEN);
    
    FEN[0][0][1][2]=1;
    FEN[0][1][0][5]=1;
    FEN[0][2][1][3]=1;
    FEN[0][4][0][4]=1;
    FEN[1][2][1][1]=1;
    FEN[1][3][1][1]=1;
    FEN[1][5][0][2]=1;
    FEN[1][7][1][4]=1;
    FEN[2][0][1][1]=1;
    FEN[2][1][1][3]=1;
    FEN[2][2][1][1]=1;
    FEN[2][3][1][1]=1;
    FEN[2][4][0][3]=1;
    FEN[2][6][0][1]=1;
    FEN[2][7][1][3]=1;
    FEN[3][0][0][2]=1;
    FEN[3][1][0][0]=1;
    FEN[3][2][1][4]=1;
    FEN[3][6][0][3]=1;
    FEN[4][3][0][2]=1;
    FEN[5][2][0][2]=1;
    FEN[5][3][1][4]=1;
    FEN[5][4][1][4]=1;
    FEN[5][5][0][0]=1;
    FEN[6][0][0][0]=1;
    FEN[6][3][1][5]=1;
    FEN[6][6][1][2]=1;
    FEN[6][7][1][3]=1;
    FEN[7][1][0][4]=1;
    FEN[7][4][1][4]=1;
    FEN[7][7][1][4]=1;
}

void initialize_FEN_to::puzzle_5(uint64_t Board[12] )
{
    bool FEN[8][8][2][6];
    puzzle_5(FEN);
    translate(FEN,Board);
}

void initialize_FEN_to::position_2(uint64_t Board[12] )
{
    Standartboard(Board);
    Board[0]=0B000111100000000111100000000000000000000000000000ULL>>8*2;
    Board[2]=0B000000000010010000000000000000000000000000000000ULL>>8*2;
    Board[6]=0B0000000011101111000100000000000000000000000000000000000000000000ULL;
    Board[8]=0B0100000000000000000001000000000000000000000000000000000000000000ULL;
}

//void initialize_FEN_to::random_position( uint64_t Board[12], int max_eval_diff=INT_MAX, int num_of_pieces=32, bool pawn=1, bool rook=1, bool knight=1, bool bishop=1, bool queen=1);

void initialize_FEN_to::queen_mate(uint64_t Board[12])
{
    empty(Board);
    Board[5]=1Ull << 1;
    Board[11]=1Ull << 7*8+6;
    Board[4]=1Ull << 2;
}

void initialize_FEN_to::rook_mate(uint64_t Board[12])
{
    empty(Board);
    Board[5]=1Ull << 1;
    Board[11]=1Ull << 7*8+6;
    Board[1]=1Ull << 2;
}

void initialize_FEN_to::kn_bishop_mate(uint64_t Board[12])
{
    empty(Board);
    Board[5]=1Ull << 1;
    Board[11]=1Ull << 7*8+6;
    Board[2]=1Ull << 2;
    Board[3]=1Ull << 3;
}

void initialize_FEN_to::bishop_pair_mate(uint64_t Board[12])
{
    empty(Board);
    Board[5]=1Ull << 1;
    Board[11]=1Ull << 7*8+6;
    Board[3]=1Ull << 2;
    Board[3]|=1Ull << 3;
}

void initialize_FEN_to::rook_vs_queen_mate(uint64_t Board[12])
{
    empty(Board);
    Board[5]=1Ull << 1;
    Board[11]=1Ull << 4*8+4;
    Board[7]=1Ull << 4*8+5;
    Board[4]=1Ull << 3;
}

void initialize_FEN_to::ruy_lopez_berlin_defense(uint64_t Board[12])
{
    BB* temp=new BB;
    FEN_to_BB("r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",temp);
    for(int i=0;i<12;i++)
    Board[i]=temp->Board[i];
    delete temp;

}

void initialize_FEN_to::kings_gambit(uint64_t Board[12])
{
    BB* temp=new BB;
    FEN_to_BB("rnbqkbnr/pppp1p1p/8/8/4PppP/5N2/PPPP2P1/RNBQKB1R w KQkq - 0 5",temp);
    for(int i=0;i<12;i++)
    Board[i]=temp->Board[i];
    delete temp;

}

void initialize_FEN_to::four_knights_scotch(uint64_t Board[12])
{
    BB* temp=new BB;
    FEN_to_BB("r1bqkb1r/pppp1ppp/2n2n2/8/3pP3/2N2N2/PPP2PPP/R1BQKB1R w KQkq - 0 5",temp);
    for(int i=0;i<12;i++)
    Board[i]=temp->Board[i];
    delete temp;

}

void initialize_FEN_to::caro_can(uint64_t Board[12])
{
    BB* temp=new BB;
    FEN_to_BB("rn1qkbnr/pp2pppp/2p5/3pPb2/3P4/8/PPP2PPP/RNBQKBNR w KQkq - 1 4",temp);
    for(int i=0;i<12;i++)
    Board[i]=temp->Board[i];
    delete temp;

}




#endif // BITBOARD_INITIALISATIONS_CPP