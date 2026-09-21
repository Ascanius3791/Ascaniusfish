// OWNERSHIP=Ascanius
#ifndef ascaniusfish
#define ascaniusfish
#include<iostream>
#include<vector>
#include<unistd.h>
#include<fstream>
#include<cstdio>
#include<bitset>
#include<cstdint>
#include<limits.h>
#include<chrono>
#include<cmath>
#include "src/Bitboards.cpp"
#include "src/Bitboard_initialisations.cpp"
#include "src/Settings.cpp"
#include "src/Weights.cpp"
#include "src/templates.cpp"
#include "src/timers.cpp"
#include "src/printing.cpp"
#include "src/checks.cpp"
#include "src/python_communication.cpp"
#include "src/lookup_table.cpp"
#include "src/move_generation.cpp"
#include "src/basic_eval.cpp"
#include "src/saefty_checks.cpp"
#include "src/eval.cpp"


using namespace std;
bool error_detected=0;

vector<BB> history(0);

BB Standartboard;

class CBE //combines eval, and position,and key(for later hash funktion)
    {
        public:
        uint64_t Board[12];
        bool castle[2][2];
        bool en_peasent;
        int pos_x;
        int pos_y;
        int move;
        bool white_move;
        bool has_not_been_evaluated;
        int eval;
        
        bool is_filled;
        
            int key=0;

            bool k_w = false;
            bool b_k = false;
            int num_wp;
            int num_bp;
            int num_wB;
            int num_bB;
            int num_wKn;
            int num_bKn;
            int num_wR;
            int num_bR;
            int num_wQ;
            int num_bQ;
        
        CBE()
        {
            castle[0][0]=true;castle[0][1]=true;castle[1][0]=true;castle[1][1]=true;
            en_peasent=false;
            pos_x=0;pos_y=0;move=1;white_move=true;has_not_been_evaluated=true;eval=0;key=0;is_filled=false;
            //initialize_FEN_to_standartboard(&Fen);//delete this this is for testing, it wastes time!
            initialize_FEN_to::empty(Board);
            num_wp = 0;
            num_bp = 0;
            num_wB = 0;
            num_bB = 0;
            num_wKn = 0;
            num_bKn = 0;
            num_wR = 0;
            num_bR = 0;
            num_wQ = 0;
            num_bQ = 0;
        }
    };
   
bool is_a_capture_avalable(const uint64_t Board[12], bool white_move)
{
    uint64_t white_pieces = Board[0]|Board[1]|Board[2]|Board[3]|Board[4]|Board[5];
    uint64_t black_pieces = Board[6]|Board[7]|Board[8]|Board[9]|Board[10]|Board[11];
    
    uint64_t attacks_white = attacks_by_col(Board,1);
    uint64_t attacks_black = attacks_by_col(Board,0);

    uint64_t white_captures = attacks_white & black_pieces;
    uint64_t black_captures = attacks_black & white_pieces;
    cout << "In this position, ";
    print(Board);
    cout << "white captures: " << endl;
    print(white_captures);
    cout << "black captures: " << endl;
    print(black_captures);
    if(white_move)
    return white_captures;
    return black_captures;
}

inline bool captures_more_valuable_piece(const BB* const parent, const BB* const child, const WEIGHTS& W = WEIGHTS_OG)
{
    const int own_offset = 6*!parent->white_move;
    const int enemy_offset = 6*parent->white_move;
    int moving_piece = -1;
    int captured_piece = -1;

    for(int piece=0;piece<6;piece++)
    {
        if(parent->Board[piece+own_offset] & ~child->Board[piece+own_offset])
        moving_piece=piece;

        if(parent->Board[piece+enemy_offset] & ~child->Board[piece+enemy_offset])
        captured_piece=piece;
    }

    if(moving_piece==-1 || captured_piece==-1)
    return false;

    return W.piece_value[captured_piece] > W.piece_value[moving_piece];
}

inline int resolved_assigned_depth(const int assigned_depth, const int parent_depth)
{
    return assigned_depth==INT_MAX ? parent_depth : assigned_depth;
}

vector<int> assign_depth(const BB* const original, const BB* const wfh, int number_of_new_moves, const int free_depth, const WEIGHTS& W = WEIGHTS_OG)
{
    uint64_t original_own_P = original->Board[0+6*!original->white_move]|original->Board[1+6*!original->white_move]|original->Board[2+6*!original->white_move]|original->Board[3+6*!original->white_move]|original->Board[4+6*!original->white_move]|original->Board[5+6*!original->white_move];
    uint64_t original_enemy_P = original->Board[0+6*original->white_move]|original->Board[1+6*original->white_move]|original->Board[2+6*original->white_move]|original->Board[3+6*original->white_move]|original->Board[4+6*original->white_move]|original->Board[5+6*original->white_move];
    //uint64_t original_own_attacks = attacks_by_col(original->Board,original->white_move);
    //if the move was a capture use the free depth
    vector<int> depth(number_of_new_moves);
    float depth_measure[number_of_new_moves],total_depth_measure=1;
    if(free_depth<1)
    {
        cout << "Error in assign_depth\n";
        cout << "Free depth = " << free_depth << endl;
        exit(1);
    }
    for(int i=0;i<number_of_new_moves;i++)
    {
        if(captures_more_valuable_piece(original,wfh+i,W))
        {
            depth[i]=INT_MAX;
            depth_measure[i]=0;
            continue;
        }

        uint64_t own_P = wfh[i].Board[0+6*!original->white_move]|wfh[i].Board[1+6*!original->white_move]|wfh[i].Board[2+6*!original->white_move]|wfh[i].Board[3+6*!original->white_move]|wfh[i].Board[4+6*!original->white_move]|wfh[i].Board[5+6*!original->white_move];
        uint64_t enemy_P = wfh[i].Board[0+6*original->white_move]|wfh[i].Board[1+6*original->white_move]|wfh[i].Board[2+6*original->white_move]|wfh[i].Board[3+6*original->white_move]|wfh[i].Board[4+6*original->white_move]|wfh[i].Board[5+6*original->white_move];
        
        //if(original_enemy_P==enemy_P)
        {
            depth_measure[i] =tactical_potential(wfh[i].Board);
            total_depth_measure += depth_measure[i];
        }
        //else 
        //depth_measure[i]=-1;
        
    }

    for(int i=0;i<number_of_new_moves;i++)
    {
        if(depth[i]==INT_MAX)
        continue;

        //if(depth_measure[i]!=-1)
        depth[i]=free_depth*(depth_measure[i]/total_depth_measure);
        //else 
        //depth[i]=free_depth;
    }
    for(int i=0;i<number_of_new_moves;i++)
    {
        if(depth[i]<0)
        {
            cout << "Error in assign_depth\n";
            cout << "Depth = " << depth[i] << endl;
            exit(1);
        }
    }
    return depth;
}

void initialize_FEN_to::random_position( uint64_t Board[12], int max_eval_diff, int num_of_pieces, bool pawn, bool rook, bool knight, bool bishop, bool queen)
{
    int eval_diff=INT_MAX;
    do
    {
        
        empty(Board);
        uint64_t allowed_squares=~(0Ull);

        for(int n=0;n<2;)
        {
            int i=rand()%64;
            if(allowed_squares & 1Ull << i)
            {
                Board[6*n+5]=1Ull << i;
                allowed_squares=allowed_squares & ~(1Ull << i);
                n++;
            }
        }

        for(int n=0;n<num_of_pieces-2;)
        {

            int i=rand()%64;
            int piece;
            do
            {
                i=rand()%64;
                piece=rand()%11;
            } while ((i<8 || i>=56) && (piece%6==0) || (piece%6==0 && pawn==false) || (piece%6==1 && rook==false) || (piece%6==2 && knight==false) || (piece%6==3 && bishop==false) || (piece%6==4 && queen==false) || piece==5);


            if(allowed_squares & 1Ull << i)
            {
                Board[piece]=Board[piece] | 1Ull << i;
                allowed_squares=allowed_squares & ~(1Ull << i);
                n++;
            }
        }
        
        BB* temp = new BB;
        for(int i=0;i<12;i++)
        temp->Board[i]=Board[i];
        eval_diff=eval(temp);
        if(eval_diff==INT_MIN)
        eval_diff++;
        if(eval_diff==INT_MAX)
        eval_diff--;
        eval_diff=abs(eval_diff);
        //cout << "Eval diff: " << eval_diff << "\n";
        delete temp;
    }while (eval_diff>max_eval_diff);
    
    
    
}

void invert_colour(BB &original)// malfunctioning!!!!!!!!! also changes the board orientation(meaning white and black switch places else castling and pawn direction are messed up)
{
    BB temp;
    for(int i=0;i<12;i++)
    temp.Board[i]=original.Board[i];
    initialize_FEN_to::empty(original.Board);
    uint64_t mirror = 0B11111111Ull;
    for(int i=0;i<6;i++)
    {
        for(int m=0;m<8;m++)
        {
            original.Board[i+6] |= (temp.Board[i] & mirror << 8*m) << 8*(7-m);


            original.Board[i] |= (temp.Board[i+6] & mirror << 8*m) << 8*(7-m);
        }
        

    }
    temp.castle[0][0]=original.castle[0][0];
    temp.castle[0][1]=original.castle[0][1];
    temp.castle[1][0]=original.castle[1][0];
    temp.castle[1][1]=original.castle[1][1];

    original.white_move=!original.white_move;
    original.en_passant=0;
    original.castle[0][0]=temp.castle[1][0];
    original.castle[0][1]=temp.castle[1][1];
    original.castle[1][0]=temp.castle[0][0];
    original.castle[1][1]=temp.castle[0][1];
    
    
}

void interpret_eval(int eval)
{
    if(eval==INT_MAX/2)
    {
        cout << "No evaluation available\n";
        return;
    }
    if(eval <= INT_MIN + max_mating_seq)
    {
        cout << "\nCheckmate for black in: \n";
        cout << (eval - INT_MIN) << " half moves\n";
        return;
    }
    if(eval >= INT_MAX - max_mating_seq)
    {
        cout << "\nCheckmate for white in: \n";
        cout << (INT_MAX -eval) << " half moves\n";
        return;
    }
        cout<<"The evaluation is: "<<eval<<"\n";
    
}

string get_move(const BB* const original, const BB* const goal )
    {
        bool WM=original->white_move;
        int start=-1;
        int end=-1;
        int promotion_type=-1;
        uint64_t original_friendly_piece=0, original_enemy_piece=0, goal_friendly_piece=0, goal_enemy_piece=0;
        if(original->castle[WM][0] && !goal->castle[WM][0] && goal->Board[5+6*!WM] & 1Ull << 2+7*8*!WM)//if the king appears on the square it stands one ofter castling, ajust lost its castling right, it must have castled
        {
            return "O-O-O";
        }
        if(original->castle[WM][1] && !goal->castle[WM][1] && goal->Board[5+6*!WM] & 1Ull << 6+7*8*!WM)
        {
            return "O-O";
        }
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
        BB* wfh=new BB[300];
        auto result = all_moves(original,wfh);
        int number_of_new_moves = std::get<0>(result);
        int moved_piece=-1;
        for(int i=0;i<6;i++)
        {
            if(original->Board[i+6*!WM] & (1ULL<<start))
            {
                moved_piece=i;
                break;
            }
        }
        vector<string> UCI_moves;
        vector<int> start_sq;
        vector<int> end_sq;
        for(int i=0;i<number_of_new_moves;i++)
        {
            UCI_moves.push_back(get_UCI(original,wfh+i));
            start_sq.push_back(UCI_moves[i][0]-'a'+8*(UCI_moves[i][1]-'1'));
            end_sq.push_back(UCI_moves[i][2]-'a'+8*(UCI_moves[i][3]-'1'));
        }
        vector<string> UCI_moves_with_same_end;
        for(int i=0;i<number_of_new_moves;i++)
        {
            if(end_sq[i]==end)
            UCI_moves_with_same_end.push_back(UCI_moves[i]);
        }
        vector<string> UCI_moves_with_same_end_and_same_piece;
        for(int i=0;i<int(UCI_moves_with_same_end.size());i++)
        {
            if(1ULL << (UCI_moves_with_same_end[i][0]-'a'+8*(UCI_moves_with_same_end[i][1]-'1')) & original->Board[moved_piece+6*!WM])
            UCI_moves_with_same_end_and_same_piece.push_back(UCI_moves_with_same_end[i]);
        }        

        vector<string> UCI_moves_with_same_end_and_same_piece_and_same_file;//are there confusable moves?
        vector<string> UCI_moves_with_same_end_and_same_piece_and_same_rank;
        
        
        for(int i=0;i<int(UCI_moves_with_same_end_and_same_piece.size());i++)
        {
            if((UCI_moves_with_same_end_and_same_piece[i][0]-'a'+8*(UCI_moves_with_same_end_and_same_piece[i][1]-'1'))/8==start/8)
            UCI_moves_with_same_end_and_same_piece_and_same_rank.push_back(UCI_moves_with_same_end_and_same_piece[i]);
            
            if((UCI_moves_with_same_end_and_same_piece[i][0]-'a'+8*(UCI_moves_with_same_end_and_same_piece[i][1]-'1'))%8==start%8)
            UCI_moves_with_same_end_and_same_piece_and_same_file.push_back(UCI_moves_with_same_end_and_same_piece[i]);
        }


        string move;
        switch (moved_piece)
        {
        case 0:
            if(original_enemy_piece != goal_enemy_piece)
            move+='a'+start%8;
            break;
        case 1:
            move+='R';
            break;
        case 2:
            move+='N';
            break;
        case 3:
            move+='B';
            break;
        case 4:
            move+='Q';
            break;
        case 5:
            move+='K';
            break;
        default:
            break;
        }
        
        if(UCI_moves_with_same_end_and_same_piece.size()>1)
        if (UCI_moves_with_same_end_and_same_piece_and_same_file.size() > 1 && UCI_moves_with_same_end_and_same_piece_and_same_rank.size() > 1) {
            move += char('a' + start % 8);
            move += char('1' + start / 8);
        } else if (UCI_moves_with_same_end_and_same_piece_and_same_file.size() == 1) {
            move += char('a' + start % 8);
        } else if (UCI_moves_with_same_end_and_same_piece_and_same_rank.size() == 1) {
            move += char('1' + start / 8);
        }
        
        if(original_enemy_piece!=goal_enemy_piece)
        {
            move+='x';
        }
        move+=char('a'+end%8);
        move+=char('1'+end/8);

        if(promotion_type!=-1)
        {
            if(promotion_type==4+6*!WM)
            move+="=Q";
            if(promotion_type==1+6*!WM)
            move+="=R";
            if(promotion_type==3+6*!WM)
            move+="=B";
            if(promotion_type==2+6*!WM)
            move+="=N";
        }
        if(exception_eval(goal)==2)
        {
            move+='#';
        }
        
        else if(in_check(goal->Board,WM))
        {
            move+='+';
        }
        if(exception_eval(goal)==1)
        {
            move+=" 1/2-1/2";
        }
        if(exception_eval(goal)==2)
        {
            if(!WM)
            move+=" 0-1";
            else
            move+=" 1-0";
        }
        return move;
        
    }

string get_PGN(vector<BB> history,string FEN="")
    {
        string PGN="";
        if(FEN!="")
        PGN+="[FEN \""+FEN+"\"]\n";
        for(int i=0;i<int(history.size()-1);i++)
        {
            if(i%2==0)
            PGN+=to_string(i/2+1)+". ";
            PGN+=get_move(&history[i],&history[i+1])+" ";
        }
        return PGN;
    }


#endif
