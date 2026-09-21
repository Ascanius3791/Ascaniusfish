// OWNERSHIP=Ascanius
#ifndef NNUE_CPP
#define NNUE_CPP

#include "../lib/NNUE.hpp"

bool* BB::to_bool_board()
{
    bool temp[8][8][2][6];
    initialize_FEN_to::translate(Board,temp);
    bool* bool_board = new bool[8*8*2*6+5];//boardsize +castling + whose turn
    for(int i=0;i<8;i++)
    {
        for(int j=0;j<8;j++)
        {
            for(int k=0;k<2;k++)
            {
                for(int l=0;l<6;l++)
                {
                    bool_board[i+8*j+64*k+128*l]=temp[i][j][k][l];
                }
            }
        }
    }
    bool_board[8*8*2*6]=white_move;
    bool_board[8*8*2*6+1]=castle[0][0];
    bool_board[8*8*2*6+2]=castle[0][1];
    bool_board[8*8*2*6+3]=castle[1][0];
    bool_board[8*8*2*6+4]=castle[1][1];
    return bool_board;

}



















#endif
