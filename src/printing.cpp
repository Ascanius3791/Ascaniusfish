#ifndef PRINTING_CPP
#define PRINTING_CPP
#include "../lib/printing.hpp"


void print(int piece, int piece_table_value_opening[7][64], float weight_on_opening, int piece_table_value_endgame[7][64])//last piece is the black pawn
{
    int printable[7][64];
    
    for(int i=0;i<7;i++)
    for(int j=0;j<64;j++)
    {
        if(piece_table_value_endgame!=0)
        printable[i][j]=piece_table_value_opening[i][j]*(weight_on_opening)+piece_table_value_endgame[i][j]*(1-weight_on_opening);
        else
        printable[i][j]=piece_table_value_opening[i][j];
    }
                
    for(int i=7;i>=0;i--)
    {
        for(int j=0;j<8;j++)
        {
            
            int value=printable[piece][i*8+j];
            if(value>0)
            std::cout << " ";
            if(abs(value)<10)
            std::cout << " ";
            if(abs(value)<100)
            std::cout << " ";
            std::cout << value;
            std::cout << " ";

        }
        std::cout << "\n";
    }
}

bool print(const uint64_t Board[12], bool comment, std::string my_comment)//returns 0 if not ecxaption has been found 1 else
    {
        if(surpress_print_globally)
        return 0;
        if(comment)
        std::cout << std::endl << my_comment;
    bool returnvalue=0;
        std::cout << "\n";
        for(int i=7;i>=0;i--)
        {

            std::cout << std::endl ;
            if(pretty_mode)
            std::cout << std::endl;
            
            
            std::cout << i+1;
            
            for(int j=0;j<8;j++)
            {
                //print w for white piece and b for black piece
                
                std::cout << " ";
            
                int count_white=0;
                int count_black=0;
                int count_pieces=0;
                int count_pawn=0;
                int count_rook=0;
                int count_knight=0;
                int count_bishop=0;
                int count_queen=0;
                int count_king=0;
                for( int piece=0;piece<6;piece++)
                {
                    
                if(Board[piece] & (1ULL <<8*i+j)) 
                {
                    std::cout << "w";
                    count_white++;
                }
                if(Board[piece+6] & (1ULL <<8*i+j)) 
                {
                    std::cout << "b";
                    count_black++;
                }
                

                }
                std::string output="   ";
                for(int col=0;col<2;col++)
                {
                    if(Board[0+6*col] & (1ULL <<8*i+j)) 
                {
                    output = "P ";
                    count_pawn++;
                }
                if(Board[1+6*col] & (1ULL <<8*i+j)) 
                {
                    output = "R ";
                    count_rook++;
                }
                if(Board[2+6*col] & (1ULL <<8*i+j)) 
                {
                    output = "Kn";
                    count_knight++;
                }
                if(Board[3+6*col] & (1ULL <<8*i+j)) 
                {
                    output = "B ";
                    count_bishop++;
                }
                if(Board[4+6*col] & (1ULL <<8*i+j)) 
                {
                    output = "Q ";
                    count_queen++;
                }
                if(Board[5+6*col] & (1ULL <<8*i+j)) 
                {
                    output = "K ";
                    count_king++;
                }                 
                }
                count_pieces=count_black+count_white;
                //check, if filed is occupied
                    //std::cout << std::endl << std::endl << overlap_check << std::endl << std::endl;
                if(count_pieces>1)
                {
                    returnvalue =1;
                    std::cout << std::endl << count_pieces << " times overlap on fieled" << i << j << std::endl;
                    if(count_white>0) std::cout << "count_white = " << count_white << std::endl;
                    if(count_black>0) std::cout << "count_black = " << count_black << std::endl;
                    if(count_pieces>0) std::cout << "count_pieces = " << count_pieces << std::endl;
                    if(count_pawn>0) std::cout << "count_pawn = " << count_pawn << std::endl;
                    if(count_rook>0) std::cout << "count_rook = " << count_rook << std::endl;
                    if(count_knight>0) std::cout << "count_knight = " << count_knight << std::endl;
                    if(count_bishop>0) std::cout << "count_bishop = " << count_bishop << std::endl;
                    if(count_queen>0) std::cout << "count_queen = " << count_queen << std::endl;
                    if(count_king>0) std::cout << "count_king = " << count_king << std::endl;
                    
                    
                }
                    else
                    {
                        std::cout << output; 
                    }
                            
            }
            
            
        }
        std::cout << "\n  ";
        if(!pretty_mode)
        {
        for(int j=0;j<8;j++)
            {
                std::cout << j+1 << "   ";
            } 
        }
        if(pretty_mode)
        {
            for(int j=0;j<8;j++)
            {
                if(j==0)
                std::cout << "A" << "   ";
                if(j==1)
                std::cout << "B" << "   ";
                if(j==2)
                std::cout << "C" << "   ";
                if(j==3)
                std::cout << "D" << "   ";
                if(j==4)
                std::cout << "E" << "   ";
                if(j==5)
                std::cout << "F" << "   ";
                if(j==6)
                std::cout << "G" << "   ";
                if(j==7)
                std::cout << "H" << "   ";
                
            }
        }
        return returnvalue;

    }
    
bool print(const uint64_t Board, bool comment, std::string my_comment)//returns 0 if not ecxaption has been found 1 else
    {
        if(surpress_print_globally)
        return 0;
        if(comment)
        std::cout << std::endl << my_comment;
    bool returnvalue=0;
        std::cout << "\n";
        for(int i=7;i>=0;i--)
        {
            std::cout << std::endl ;
            if(pretty_mode)
            std::cout << std::endl;
            std::cout << i+1;
            
            for(int j=0;j<8;j++)          
            if(Board & (1ULL <<8*i+j)) 
            std::cout << " wP ";
            else std::cout << "    ";
                
            }
        std::cout << "\n  ";
        if(!pretty_mode)
        {
        for(int j=0;j<8;j++)
            {
                std::cout << j+1 << "   ";
            } 
        }
        if(pretty_mode)
        {
            for(int j=0;j<8;j++)
            {
                if(j==0)
                std::cout << "A" << "   ";
                if(j==1)
                std::cout << "B" << "   ";
                if(j==2)
                std::cout << "C" << "   ";
                if(j==3)
                std::cout << "D" << "   ";
                if(j==4)
                std::cout << "E" << "   ";
                if(j==5)
                std::cout << "F" << "   ";
                if(j==6)
                std::cout << "G" << "   ";
                if(j==7)
                std::cout << "H" << "   ";
                
            }
        }
        return returnvalue;

    }

void print(const bool FEN[8][8][2][6])
    {
        if(surpress_print_globally)
        return;
        
        std::cout << "\n";
        for(int i=7;i>=0;i--)
        {

            std::cout << std::endl ;
            if(pretty_mode)
            std::cout << std::endl;
            
            
            std::cout << i+1;
            
            for(int j=0;j<8;j++)
            {
                //print w for white piece and b for black piece
                
                std::cout << " ";
            
                int count_white=0;
                int count_black=0;
                int count_pieces=0;
                int count_pawn=0;
                int count_rook=0;
                int count_knight=0;
                int count_bishop=0;
                int count_queen=0;
                int count_king=0;
                for( int piece=0;piece<6;piece++)
                {
                    
                if(FEN[i][j][0][piece]) 
                {
                    std::cout << "w";
                    count_white++;
                }
                if(FEN[i][j][1][piece]) 
                {
                    std::cout << "b";
                    count_black++;
                }
                

                }
                std::string output="   ";
                for(int col=0;col<2;col++)
                {
                    if(FEN[i][j][col][0]) 
                {
                    output = "P ";
                    count_pawn++;
                }
                if(FEN[i][j][col][1]) 
                {
                    output = "R ";
                    count_rook++;
                }
                if(FEN[i][j][col][2]) 
                {
                    output = "Kn";
                    count_knight++;
                }
                if(FEN[i][j][col][3]) 
                {
                    output = "B ";
                    count_bishop++;
                }
                if(FEN[i][j][col][4]) 
                {
                    output = "Q ";
                    count_queen++;
                }
                if(FEN[i][j][col][5]) 
                {
                    output = "K ";
                    count_king++;
                }                 
                }
                count_pieces=count_black+count_white;
                //check, if filed is occupied
                    //std::cout << std::endl << std::endl << overlap_check << std::endl << std::endl;
                if(count_pieces>1)
                {
                    std::cout << std::endl << count_pieces << " times overlap on fieled" << i << j << std::endl;
                    if(count_white>0) std::cout << "count_white = " << count_white << std::endl;
                    if(count_black>0) std::cout << "count_black = " << count_black << std::endl;
                    if(count_pieces>0) std::cout << "count_pieces = " << count_pieces << std::endl;
                    if(count_pawn>0) std::cout << "count_pawn = " << count_pawn << std::endl;
                    if(count_rook>0) std::cout << "count_rook = " << count_rook << std::endl;
                    if(count_knight>0) std::cout << "count_knight = " << count_knight << std::endl;
                    if(count_bishop>0) std::cout << "count_bishop = " << count_bishop << std::endl;
                    if(count_queen>0) std::cout << "count_queen = " << count_queen << std::endl;
                    if(count_king>0) std::cout << "count_king = " << count_king << std::endl;
                    
                    
                }
                    else
                    {
                        std::cout << output; 
                    }
                            
            }
            
            
        }
        std::cout << "\n  ";
        if(!pretty_mode)
        {
        for(int j=0;j<8;j++)
            {
                std::cout << j+1 << "   ";
            } 
        }
        if(pretty_mode)
        {
            for(int j=0;j<8;j++)
            {
                if(j==0)
                std::cout << "A" << "   ";
                if(j==1)
                std::cout << "B" << "   ";
                if(j==2)
                std::cout << "C" << "   ";
                if(j==3)
                std::cout << "D" << "   ";
                if(j==4)
                std::cout << "E" << "   ";
                if(j==5)
                std::cout << "F" << "   ";
                if(j==6)
                std::cout << "G" << "   ";
                if(j==7)
                std::cout << "H" << "   ";
                
            }
        }
        
    
    }

void print(const std::vector<BB>all_boards, int N)
{
    for(int i=0;i<N;i++)
    print(all_boards[i].Board);
}

void print(const BB* const Base, int start, int end)
{
    for(int i=start;i<end;i++)
    print(Base[i].Board);
}
























#endif // PRINTING_CPP