#ifndef BITBOARDS_CPP
#define BITBOARDS_CPP
#include "../lib/Bitboards.hpp"
#include <string>

//members
// constructors

BB::BB()
{
    for(int i=0;i<12;i++)
    {
        Board[i]=0;
    }
    white_move=1;
    for(int i=0;i<2;i++)
    {
        for(int j=0;j<2;j++)
        {
            castle[i][j]=true;
        }
    }
    en_passant=0;
    number_of_repetitions=0;
    is_evaluated=0;
    move=1;
    halfmoves_since_last_capture_or_pawn_move=0;
    eval=1000;
    depth_of_eval=0;
    //parent=NULL;
    //alpha=INT_MIN;
    //beta=INT_MAX;
}

void FEN_to_BB(const std::string FEN, BB* const original)
{
    for(int i=0;i<12;i++)
    original->Board[i]=0;
    original->castle[0][0]=0;//these used to be set to 1, but i belive 0 is the correct initilisation, as we set it to 1, if K,Q,k,q is in the FEN
    original->castle[0][1]=0;
    original->castle[1][0]=0;
    original->castle[1][1]=0;
    original->en_passant=0;
    
    int i=0;
    int j=0;
    int i_on_previous_iteration;
    while(FEN[i]!=' ')
    {
        i_on_previous_iteration=i;
        if(FEN[i]=='/')
        {
            i++;
        }
        if(FEN[i]>='1' && FEN[i]<='8')
        {
            j+=FEN[i]-'0';
            i++;
        }
             
        int virtual_file=j%8;
        int virtual_rank=7-j/8;
        
        if(FEN[i]=='P')
        {
            original->Board[0] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
            
        }
        if(FEN[i]=='R')
        {
            original->Board[1] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='N')
        {
            original->Board[2] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='B')
        {
            original->Board[3] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='Q')
        {
            original->Board[4] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='K')
        {
            original->Board[5] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='p')
        {
            original->Board[6] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='r')
        {
            original->Board[7] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='n')
        {
            original->Board[8] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='b')
        {
            original->Board[9] |= 1ULL<<(virtual_file+8*virtual_rank);
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='q')
        {
            original->Board[10] |= 1ULL<<(virtual_file+8*virtual_rank);;
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(FEN[i]=='k')
        {
            original->Board[11] |= 1ULL<<(virtual_file+8*virtual_rank);;
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
        if(i_on_previous_iteration==i)
        {
            i++;
            virtual_file=++j%8;
            virtual_rank=7-j/8;
        }
    }
    
    i++;
    if(FEN[i]=='w')
    original->white_move=1;
    else
    original->white_move=0;
    i+=2;
    if(FEN[i]=='K')
    {
        original->castle[1][1]=1;
        i++;
    }
    if(FEN[i]=='Q')
    {
        original->castle[1][0]=1;
        i++;
    }
    if(FEN[i]=='k')
    {
        original->castle[0][1]=1;
        i++;
    }
    if(FEN[i]=='q')
    {
        original->castle[0][0]=1;
        i++;
    }
    if(FEN[i]=='-')
    {
        original->castle[0][1]=0;
        original->castle[0][0]=0;
        original->castle[1][1]=0;
        original->castle[1][0]=0;
        i=i+2;
    }
    else i++;
    if(FEN[i]=='-')
    original->en_passant=0;
    else
    {
        original->en_passant=1ULL << (FEN[i]-'a')+(8*(FEN[i+1]-'1'));
        i++;
    }
    i+=2;
    original->halfmoves_since_last_capture_or_pawn_move=FEN[i]-'0';
    i+=2;
    original->move=FEN[i]-'0';
}

BB::BB(const std::string FEN)
{
    FEN_to_BB(FEN,this);//this is maybe ineficcent, but it is called rarely
    eval=1000;
    depth_of_eval=0;
}

void copy_BB(const BB* const original ,BB* const goal)
    {
        for(int piece=0;piece<12;piece++)
        goal->Board[piece]=original->Board[piece];
        
        goal->castle[0][0]=original->castle[0][0];
        goal->castle[0][1]=original->castle[0][1];
        goal->castle[1][0]=original->castle[1][0];    
        goal->castle[1][1]=original->castle[1][1];

        goal->en_passant=original->en_passant;
        goal->white_move=original->white_move;
        goal->number_of_repetitions=original->number_of_repetitions;
        goal->move=original->move;
        goal->is_evaluated=original->is_evaluated;
        goal->eval=original->eval;
        goal->depth_of_eval=original->depth_of_eval;
       // goal->parent=original->parent;
    }

void Base_BB(const BB* const original, BB* const goal)
    {
        for(int i=0;i<12;i++)
        goal->Board[i]=original->Board[i];
        
        goal->white_move=!original->white_move;
        goal->castle[0][0]=original->castle[0][0];
        goal->castle[0][1]=original->castle[0][1];
        goal->castle[1][0]=original->castle[1][0];
        goal->castle[1][1]=original->castle[1][1];
        goal->en_passant=0;
        goal->number_of_repetitions=original->number_of_repetitions;
        goal->move=original->move+1;
        goal->is_evaluated=0;
        //goal->parent=original; //no parents so far-> many things can be const
    // goal->alpha=INT_MIN;
        //goal->beta=INT_MAX; 
    }

BB::BB(const BB* const original,std::string mode)
{
    if(mode!="copy" && mode!="base")
    {
        throw std::runtime_error("mode in copy constructor of BB has to be copy or base");
    }
    if(mode=="copy")
    copy_BB(original,this);
    if(mode=="base")
    Base_BB(original,this);
    //remaining initialisations
    is_evaluated=0;
    move=1;
    halfmoves_since_last_capture_or_pawn_move=0;
    eval=1000;
    depth_of_eval=0;
    
}

//operators
bool BB::operator==(const BB* const rhs) const
{
    for(int i=0;i<12;i++)
    {
        if(Board[i]!=rhs->Board[i])
        return 0;
    }
    
    if(castle[0][0]!=rhs->castle[0][0])
    return 0;
    if(castle[0][1]!=rhs->castle[0][1])
    return 0;
    if(castle[1][0]!=rhs->castle[1][0])
    return 0;
    if(castle[1][1]!=rhs->castle[1][1])
    return 0;
    if(en_passant!=rhs->en_passant)
    return 0;
    if(white_move!=rhs->white_move)
    return 0;
    return 1;
}

bool BB::operator==(const BB& rhs) const
{
    for(int i=0;i<12;i++)
    {
        if(Board[i]!=rhs.Board[i])
        return 0;
    }
    if(castle[0][0]!=rhs.castle[0][0])
    return 0;
    if(castle[0][1]!=rhs.castle[0][1])
    return 0;
    if(castle[1][0]!=rhs.castle[1][0])
    return 0;
    if(castle[1][1]!=rhs.castle[1][1])
    return 0;
    if(en_passant!=rhs.en_passant)
    return 0;
    if(white_move!=rhs.white_move)
    return 0;
    return 1;
}

// functions

std::string get_UCI(const BB* const original, const BB* const goal )
{ 
    bool WM=original->white_move;
    if(original->castle[WM][0] && !goal->castle[WM][0] && goal->Board[5+6*!WM] & 1Ull << 2+7*8*!WM)//if the king appears on the square it stands one ofter castling, ajust lost its castling right, it must have castled
    {
        if(WM)
        return "e1c1";
        else
        return "e8c8";
    }
    if(original->castle[WM][1] && !goal->castle[WM][1] && goal->Board[5+6*!WM] & 1Ull << 6+7*8*!WM)
    {
        
        if(WM)
        return "e1g1";
        else
        return "e8g8";
    }
    int start=-1;
    int end=-1;
    int promotion_type=-1;
    uint64_t original_friendly_piece=0, original_enemy_piece=0, goal_friendly_piece=0, goal_enemy_piece=0;
    for(int i=0;i<6;i++)
    {
        original_friendly_piece |= original->Board[i+6*!WM];
        goal_friendly_piece |= goal->Board[i+6*!WM];
        original_enemy_piece |= original->Board[i+6*WM];
        goal_enemy_piece |= goal->Board[i+6*WM];
    }
    for(int i=0;i<64;i++)
    {
        if(original_friendly_piece & (1ULL << i) && !(goal_friendly_piece & (1ULL << i)))
        {
            start=i;
            break;
        }
    }
    for(int i=0;i<64;i++)
    {
        if(goal_friendly_piece & (1ULL << i) && !(original_friendly_piece & (1ULL << i)))
        {
            end=i;
            break;
        }
    }
    if(count(original->Board[0+6*!WM])>count(goal->Board[0+6*!WM]))//if a promotion happend
    {
        for(int i=0;i<12;i++)
        {
            if(goal->Board[i] & (1ULL<<end))
            {
                promotion_type=i;
                break;
            }
        }
    }

    std::string UCI;
    UCI+=char('a'+start%8);
    UCI+=char('1'+start/8);
    UCI+=char('a'+end%8);
    UCI+=char('1'+end/8);
    if(promotion_type!=-1)
    {
        if(promotion_type==4+6*!WM)
        UCI+='q';
        if(promotion_type==1+6*!WM)
        UCI+='r';
        if(promotion_type==3+6*!WM)
        UCI+='b';
        if(promotion_type==2+6*!WM)
        UCI+='n';
    }
    return UCI;
    
}

std::string BB::get_UCI(const BB* const goal )
{
    return ::get_UCI(this,goal);// calling the function from the global namespace aka the one on top of this definition
}

std::string get_FEN(BB original)
{
    std::string FEN;
    int empty_counter=0;
    for(int rank=7;rank>=0;rank--)
    for(int file=0;file<8;file++)
    {
        
        int i=8*rank+file;
        
        if(i%8==0 && i!=8*7)
        {
            if(empty_counter!=0)
            FEN+=std::to_string(empty_counter);
            empty_counter=0;
            FEN+='/';
        }
        int piece_type=-1;
        for(int j=0;j<12;j++)
        {
            if(original.Board[j] & (1ULL<<i))
            {
                piece_type=j;
                break;
            }
        }
        if(piece_type==-1)
        {
            empty_counter++;
            if(i==7)
            FEN+=std::to_string(empty_counter);//last element

        }
        else
        {
            if(empty_counter!=0)
            FEN+=std::to_string(empty_counter);
            empty_counter=0;
            if(piece_type==0)
            FEN+='P';
            if(piece_type==1)
            FEN+='R';
            if(piece_type==2)
            FEN+='N';
            if(piece_type==3)
            FEN+='B';
            if(piece_type==4)
            FEN+='Q';
            if(piece_type==5)
            FEN+='K';
            if(piece_type==6)
            FEN+='p';
            if(piece_type==7)
            FEN+='r';
            if(piece_type==8)
            FEN+='n';
            if(piece_type==9)
            FEN+='b';
            if(piece_type==10)
            FEN+='q';
            if(piece_type==11)
            FEN+='k';
        }
    }
    FEN+=' ';
    if(original.white_move)
    FEN+='w';
    else
    FEN+='b';
    FEN+=' ';
    if(original.castle[1][1])
    FEN+='K';
    if(original.castle[1][0])
    FEN+='Q';
    if(original.castle[0][0])
    FEN+='k';
    if(original.castle[0][1])
    FEN+='q';
    if(!original.castle[0][0] && !original.castle[0][1] && !original.castle[1][0] && !original.castle[1][1])
    FEN+='-';
    FEN+=' ';
    if(original.en_passant==0)
    FEN+='-';
    else
    {
        int square=-1;
        for(int i=0;i<64;i++)
        {
            if(original.en_passant & (1ULL<<i))
            {
                square=i;
                break;
            }
        }
        FEN+=char('a'+square%8);
        FEN+=char('1'+square/8);
    }
    FEN+=' ';
    
    FEN+=std::to_string(original.halfmoves_since_last_capture_or_pawn_move);
    FEN+=' ';
    FEN+=std::to_string(original.move);
    
    return FEN;
}

std::string BB::get_FEN()
{
    return ::get_FEN(*this);
}

void castling_right_rook_correction_for_col(BB* const original, int for_white)//
{
    if(for_white)
    {
        if(!(1Ull << 0 & (*original).Board[1]))
        (*original).castle[1][0]=0;
        if(!(1Ull << 7 & (*original).Board[1]))
        (*original).castle[1][1]=0;
    }
    if(!for_white)
    {
        if(!(1Ull << 56 & (*original).Board[7]))
        (*original).castle[0][0]=0;
        if(!(1Ull << 63 & (*original).Board[7]))
        (*original).castle[0][1]=0;
    }
}

void castling_rights(BB* const original)
{
    for( int col = 0; col<2; col++)
    {
        if(!(1Ull << !col*8*7 & (*original).Board[1+6*!col]))
        (*original).castle[col][0]=0;
        if(!(1Ull << 7+!col*8*7 & (*original).Board[1+6*!col]))
        (*original).castle[col][1]=0;
        if(!(1Ull << 4+!col*8*7 & (*original).Board[5+6*!col]))
        {
            (*original).castle[col][0]=0;
            (*original).castle[col][1]=0;
        }
    }
    

}

void BB::check_castling_rights()
{
    castling_rights(this);
    castling_right_rook_correction_for_col(this,1);
    castling_right_rook_correction_for_col(this,0);
}

bool are_equal(const BB* const  BB_1,const BB* const BB_2)
    {
        for(int piece=0;piece<12;piece++)
        {
            if(BB_1->Board[piece]!=BB_2->Board[piece])
            return 0;
        }
        
        if(BB_1->castle[0][0]!=BB_2->castle[0][0])
        return 0;
        if(BB_1->castle[0][1]!=BB_2->castle[0][1])
        return 0;
        if(BB_1->castle[1][0]!=BB_2->castle[1][0])
        return 0;
        if(BB_1->castle[1][1]!=BB_2->castle[1][1])
        return 0;
        if(BB_1->en_passant!=BB_2->en_passant)
        return 0;
        if(BB_1->white_move!=BB_2->white_move)
        return 0;
        return 1;
    }

    
std::string get_coordinate_PGN(std::vector<BB> history)
{
    std::string PGN="";
    for(int i=0;i<int(history.size()-1);i++)
    {
        if(i%2==0)
        PGN+=std::to_string(i/2+1)+". ";
        PGN+=get_UCI(&history[i],&history[i+1])+" ";
    }
    return PGN;
}
    






#endif // BITBOARDS_CPP