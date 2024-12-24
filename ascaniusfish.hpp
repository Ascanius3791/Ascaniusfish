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
#include "magics.hpp"
#include "src/Bitboards.cpp"
#include "src/Bitboard_initialisations.cpp"
#include "src/Settings.cpp"
#include "src/Weights.cpp"
#include "src/templates.cpp"
#include "src/timers.cpp"
#include "src/printing.cpp"
#include "src/checks.cpp"

int temp_function_counter=0;

using namespace std;
bool error_detected=0;
constexpr int max_mating_seq = 1000;

vector<BB> history(0);

BB Standartboard;
const string Standart_FEN="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

#pragma region independent_sections// these use at a max count ,BB, CBE, TM and WEIGHTS. The pragmas in here can be shifted freely, but not necessarily the function within each region(print uses other prints)
    
    string get_coordinate_PGN(vector<BB> history)
    {
        string PGN="";
        for(int i=0;i<int(history.size()-1);i++)
        {
            if(i%2==0)
            PGN+=to_string(i/2+1)+". ";
            PGN+=get_UCI(&history[i],&history[i+1])+" ";
        }
        return PGN;
    }
    
    FILE* open_python_script(string script_name ="display_board.py", string FEN=Standart_FEN) {
    string command = "python3 " + script_name + " \"" + FEN + "\"";
    FILE* pipe = popen(command.c_str(), "w");
    if (!pipe) {
        cerr << "Failed to start Python script" << endl;
    }
    return pipe;
}

    void write_to_python_script(FILE* pipe, string uci) {
        if (pipe) {
            fprintf(pipe, "%s\n", uci.c_str());
            fflush(pipe);
        } else {
            cerr << "Pipe is not open" << endl;
        }
    }

    void close_python_script(FILE* pipe) {
        if (pipe) {
            pclose(pipe);
        } else {
            cerr << "Pipe is not open" << endl;
        }
    }

//==================================================================================================================================
     
#pragma region print
    
    class lookup_table
    {
        
        inline uint64_t hash_0(const uint64_t u)
        {
            return __builtin_popcountll(u);
        }
        inline uint64_t hash_0(const uint64_t board[12])//return number of non king pieces
            {
                uint64_t n=board[0]|board[1]|board[2]|board[3]|board[4]|board[6]|board[7]|board[8]|board[9]|board[10];//here the kings are irrellevant, since they are always there.
                return hash_0(n);
            }

        inline uint64_t hash_1(const uint64_t u)
        {
            uint64_t n = u;
            n ^= n >> 33;
            n *= 0xff51afd7ed558ccd;
            n ^= n >> 33;
            n *= 0xc4ceb9fe1a85ec53;
            n ^= n >> 33;
            return n;
        }
        inline uint64_t hash_1(const uint64_t board[12])
        {
            uint64_t n=board[0]|board[1]|board[2]|board[3]|board[4]|board[5]|board[6]|board[7]|board[8]|board[9]|board[10]|board[11];
            return hash_1(n);
        }
        inline uint64_t hash_2(const uint64_t u)
        {
            uint64_t n = u;
            n ^= n >> 30;
            n *= 0xbf58476d1ce4e5b9;
            n ^= n >> 27;
            n *= 0x94d049bb133111eb;
            n ^= n >> 31;
            return n;
        }
        inline uint64_t hash_2(const uint64_t board[12])
        {
            uint64_t n=board[0]|board[1]|board[2]|board[3]|board[4]|board[5]|board[6]|board[7]|board[8]|board[9]|board[10]|board[11];
            return hash_2(n);
        }
        //we shoule keep track of the 50-move rule resets, then this could be the first hash index. this should be very!good, as then only relevant positions will be compared
        static const int size_of_dimension_0=5,size_of_dimension_1=1000, size_of_dimension_2=1000;//5 because there wont be all that mny caputes (hash_0 gives the amount of pieces left))
        vector<BB> table[size_of_dimension_0][size_of_dimension_1][size_of_dimension_2];
        int size[size_of_dimension_0][size_of_dimension_1][size_of_dimension_2];
        public:
        int number_of_inserions=0, number_of_succ_readouts=0, number_of_attemted_readouts=0;
        bool there_are_doubles()
        {
            int sum=0;
            for(int i=0;i<size_of_dimension_0;i++)
            for(int j=0;j<size_of_dimension_1;j++)
            for(int k=0;k<size_of_dimension_2;k++)
            {
                for(int l=0;l<size[i][j][k];l++)
                {
                    for(int m=l+1;m<size[i][j][k];m++)
                    {
                        if(are_equal(&table[i][j][k][l],&table[i][j][k][m]))
                        {
                            sum++;
                        }
                    }
                }
            }
            cout << "There are " << sum << " doubles in the table!" << endl;
            //cout << "There are no doubles in the table!" << endl;
            return false;
        }

       
        bool is_retrivable_eval(const BB* const original, int requestes_depth)//if possible sets eval to the value in the table, returns true if found. If the board is found, but not evaluated yet(it has to be in the current branch for that or pruned away, if it was pruned, it was an arbitrary value(and hence we cannot make further conclusions, the idead to set the eval =0 is therefore wrong!))
        {
            
            //number_of_repetitions needs a rework
            number_of_attemted_readouts++;
            int h_v_0=hash_0(original->Board)%size_of_dimension_0;
            int h_v_1=hash_1(original->Board)%size_of_dimension_1;
            int h_v_2=hash_2(original->Board)%size_of_dimension_2;
            for(int i=0;i<size[h_v_0][h_v_1][h_v_2];i++)
            {
                if(are_equal(&(table[h_v_0][h_v_1][h_v_2][i]),original))//if they are equal
                {
                    if(table[h_v_0][h_v_1][h_v_2][i].depth_of_eval>=requestes_depth)// and the depth is suficcent
                    {
                        if(table[h_v_0][h_v_1][h_v_2][i].is_evaluated==0)
                        return false;//if the board is found, but not evaluated yet, we cannot make further conclusions, as it could hvae been pruned (id does not need to be a loop!)
                        number_of_succ_readouts++;
                        original->eval=table[h_v_0][h_v_1][h_v_2][i].eval;
                        return true;
                    }
                    else 
                    return false;
                    
                }
            }
            return false;
        }



        void insert(BB original, int given_depth)
                {
                    bool is_already_in_table=0;
                    number_of_inserions++;
                    uint16_t h_v_0=hash_0(original.Board)%size_of_dimension_0;
                    uint16_t h_v_1=hash_1(original.Board)%size_of_dimension_1;
                    uint16_t h_v_2=hash_2(original.Board)%size_of_dimension_2;
        
                    for(int i=0;i<size[h_v_0][h_v_1][h_v_2];i++)
                    {
                        if(are_equal(&(table[h_v_0][h_v_1][h_v_2][i]),&original))
                        {
                            if(is_retrivable_eval(&original,given_depth)&& original.is_evaluated)
                            {
                                cout << "Error, this should not be retrivable,because then this func should never have neen called!";
                                exit(0);
                            }
                            if(given_depth<=table[h_v_0][h_v_1][h_v_2][i].depth_of_eval)
                            {
                                if(given_depth!=0)
                                exit(0);
                                cout << "given_depth, temp.depth_of_eval: " << given_depth << " " << table[h_v_0][h_v_1][h_v_2][i].depth_of_eval << endl;
                                cout << "It is possible, that the table tries to insert values of lesser depth, so this code is necessary. you may removve this message";
                                return;
                            }
                            is_already_in_table=1;
                            if(given_depth>table[h_v_0][h_v_1][h_v_2][i].depth_of_eval || original.eval>=INT_MAX-max_mating_seq || original.eval<=INT_MIN+max_mating_seq)
                            if(original.is_evaluated)
                            {
                                table[h_v_0][h_v_1][h_v_2][i].eval=original.eval;
                                table[h_v_0][h_v_1][h_v_2][i].is_evaluated=1;
                            }
                            break;
                        }
                    }
                    
                    
                    if(!is_already_in_table)
                    {
                        if(original.eval>=INT_MAX-max_mating_seq || original.eval<=INT_MIN+max_mating_seq)
                        original.depth_of_eval=max_mating_seq;
                        
                        table[h_v_0][h_v_1][h_v_2].push_back(original);
                        
                        
                    }
                    
                    if(++size[h_v_0][h_v_1][h_v_2]>100)
                    {
                        //cout << "Warning: hash table is getting large, consider increasing size_of_dimension_0, size_of_dimension_1 or size_of_dimension_2" << endl;
                        table[h_v_0][h_v_1][h_v_2].erase(table[h_v_0][h_v_1][h_v_2].begin());
                    }
                }

        void get_number_of_entrys()
        {
            int sum=0;
            for(int i=0;i<size_of_dimension_0;i++)
            for(int j=0;j<size_of_dimension_1;j++)
            for(int k=0;k<size_of_dimension_2;k++)
            {
                sum+=size[i][j][k];
            }
            cout << "Number of entrys in the lookup table: " << sum << endl;
            cout << "The average number of entrys per bucket is: " << float(sum)/(size_of_dimension_0*size_of_dimension_1*size_of_dimension_2) << endl;
        }  

        void reset()
        {
            for(int i=0;i<size_of_dimension_0;i++)
            for(int j=0;j<size_of_dimension_1;j++)
            for(int k=0;k<size_of_dimension_2;k++)
            {
                table[i][j][k].clear();
                size[i][j][k]=0;
            }
            number_of_inserions=0;
            number_of_succ_readouts=0;
            number_of_attemted_readouts=0;
        };
    };
#pragma endregion print

    uint64_t attacks_by_col(const uint64_t Board[12], bool by_white_pieces)
    {
        uint64_t occupancy = Board[0] | Board[1] | Board[2] | Board[3] | Board[4] | Board[5] | Board[6] | Board[7] | Board[8] | Board[9] | Board[10] | Board[11];
        uint64_t attacks=0;
        for(int i=0;i<64;i++)
        {
            //the pawns stuff looks weird, but is correct, that is well tested! the BP/Wp Templates seem to have been mae with checks in mind, so they seem reversed
            if(by_white_pieces)
            if(Board[0] & (1Ull << i))
            attacks |= BP_template[i];
            if(!by_white_pieces)
            if(Board[6] & (1Ull << i))
            attacks |= WP_template[i];

            if((Board[1+6*!by_white_pieces] | Board[4+6*!by_white_pieces]) & (1Ull << i))
            attacks |= get_rook_attacks(i,occupancy);
            if(Board[2+6*!by_white_pieces] & (1Ull << i))
            attacks |= Kn_template[i];
            if((Board[3+6*!by_white_pieces] | Board[4+6*!by_white_pieces]) & (1Ull << i))
            attacks |= get_bishop_attacks(i,occupancy);
            
            if(Board[5+6*!by_white_pieces] & (1Ull << i))
            attacks |= K_template[i];
        }
        return attacks;
    }

#pragma endregion independent_stuff

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
   
int all_moves(const BB* const original, BB* const wfh , int len_wfh=INT_MAX)// returns number of moves
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
     
bool one_move(const BB* const original)// returns number of moves
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

int exception_eval(const BB* const original)//0=no exception, 1=stalemate, 2=checkmate
{
    
    if(one_move(original))
    return 0;
    if(!in_check(original->Board,original->white_move))
    {
        original->eval=0;
        original->is_evaluated=true;
        return 1;
        
    }
    else
    {
        original->eval=original->white_move ? INT_MIN : INT_MAX;
        original->is_evaluated=true;
        return 2;
    }
}

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

uint64_t attacked_squares(const uint64_t Board[12], bool for_white)
{
    uint64_t Attacked_squares=0;
    uint64_t own_pawns=Board[0+!6*for_white];
    uint64_t own_knights=Board[2+!6*for_white];
    uint64_t own_bishops=Board[3+!6*for_white]|Board[4+!6*for_white];
    uint64_t own_rooks=Board[1+!6*for_white]|Board[4+!6*for_white];
    uint64_t own_king=Board[5+!6*for_white];
    
    uint64_t occupancy=Board[0]|Board[1]|Board[2]|Board[3]|Board[4]|Board[5]|Board[6]|Board[7]|Board[8]|Board[9]|Board[10]|Board[11];
    while(own_pawns)
    {
        int i=find_and_delete_trailling_1(own_pawns);
        if(for_white)
        {
            Attacked_squares |= BP_template[i];
        }
        else
        {
            Attacked_squares |= WP_template[i];
        }
    }
    while(own_knights)
    {
        int i=find_and_delete_trailling_1(own_knights);
        Attacked_squares |= Kn_template[i];
    }
    while(own_bishops)
    {
        int i=find_and_delete_trailling_1(own_bishops);
        Attacked_squares |= get_bishop_attacks(i,occupancy);
    }
    while(own_rooks)
    {
        int i=find_and_delete_trailling_1(own_rooks);
        Attacked_squares |= get_rook_attacks(i,occupancy);
    }
    int i=find_and_delete_trailling_1(own_king);
    Attacked_squares |= K_template[i];
    return Attacked_squares;

}

int piecetable(const BB* const original , const WEIGHTS W = WEIGHTS_OG)
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

int piece_activity_eval(const BB* const original, const WEIGHTS W = WEIGHTS_OG)
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

int king_safety_of_colour(const uint64_t Board[12],bool white, const WEIGHTS W =WEIGHTS_OG)
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
    ;//cout << "king is in danger: " << score << endl;
    if(score>0)//king is safe
    return W.king_safety_value*sqrt(score);
    return W.king_safety_value*score;
    
    return score;
    
}

inline int material_eval(const BB* const original, const WEIGHTS W = WEIGHTS_OG)
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

int central_pawn_presence(const BB* const original, bool white, const WEIGHTS W = WEIGHTS_OG)//positive is good for both colours
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

int pawn_struckture_eval_of_colour(const BB* const original, bool white, const WEIGHTS W = WEIGHTS_OG)//positive is good for both colours
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

int positional_eval(const BB* const original, const WEIGHTS W = WEIGHTS_OG)
{
    int score=0;
    score += pawn_struckture_eval_of_colour(original,true,W)-pawn_struckture_eval_of_colour(original,false,W);
    return score;

}

int basic_eval(const BB*const original , const WEIGHTS W = WEIGHTS_OG)// return the evaluation in centipawns
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

int tactical_potential(const uint64_t Board[12], WEIGHTS W=WEIGHTS_OG)
{
    if(Board==NULL)
    {
        cout << "Error in tactical potential\n";
        return 0;
    }
    uint64_t white_pieces = Board[0]|Board[1]|Board[2]|Board[3]|Board[4]|Board[5];
    uint64_t black_pieces = Board[6]|Board[7]|Board[8]|Board[9]|Board[10]|Board[11];
    uint64_t occupancy = white_pieces|black_pieces;
    int score=0;
    int king_s = king_safety_of_colour(Board,1,W);
    
    if(king_s<0)
    score-=king_s;
    //cout << "Tactical potential 5: " << score << endl;
    king_s = king_safety_of_colour(Board,0,W);
    
    if(king_s<0)
    score-=king_s;
    //cout << "Tactical potential 4: " << score << endl;
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
    //cout << "score after shared attacks: " << score << endl;


    uint64_t virtual_white_captures = white_captures;
    int pos_black_king = __builtin_ctzll(Board[5+6]);
    //cout << "Tactical potential 3: " << score << endl;
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
            cout << "Error in tactical potential\n";
            return 0;
        }
        score += W.piece_value[piecetype]/3;
        score += 1/(1+distance_to_king(pos_black_king,i))*50;
    }
   // cout << "score after white captures: " << score << endl;
    uint64_t virtual_black_captures = black_captures;
    int pos_white_king = __builtin_ctzll(Board[5]);
    //cout << "Tactical potential 2: " << score << endl;
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
            cout << "Error in tactical potential\n";
            return 0;
        }
        score += W.piece_value[piecetype]/3;
        score += 1/(1+distance_to_king(pos_white_king,i))*50;
    }
   // cout << "score after black captures: " << score << endl;

    

    //return tanh(score/1000.0)*1000;
    //cout << "Tactical potential 1: " << score << endl;
    int return_value = std::min(std::max(200,score),int(tanh(score/4000.0)*1000));//this function looks good(its graph) 4000 will need adjustment, as the function above changes. currently all moves seem to be about 3500-4000, wich is ofc not enough vriation
    if(return_value<0)
    {
        cout << "Tactical potential: " << return_value << endl;
        cout << "Thats an error\n";
        print(Board);
        exit(1);
    }
    if(return_value>100000)
    {
        cout << "Tactical potential: " << return_value << endl;
        cout << "Thats an error\n";
        print(Board);
        exit(1);
    }
    //cout << "Tactical potential: " << return_value << endl;
    return return_value;
}

int sorting_eval(const BB* const original, const WEIGHTS W =WEIGHTS_OG)// accelerates pruning this function has to be lightheaded(Quick to compute)
{
    //return basic_eval(original,W);
    int score=0;
    temp_function_counter++;
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
    //score += WEIGHTS_OG.check_value*(1-2*!(*original).white_move);
    score+=tactical_potential(original->Board,W)/10*(1-2*!original->white_move);//77% without this
    score+=piece_activity_eval(original,W);
    return score;
}

int eval(const BB* const original, WEIGHTS W =WEIGHTS_OG, int exception_state=-1/*the can halp reduce redundant stuff later on*/)// exception eval has duplicate functionality, but is used maby in other contexts as well (both change original.eval in the case of an exception)
{
    original->is_evaluated=1;
    if(exception_state==-1)
    exception_state = exception_eval(original);//in case of exceptions, this handels the assignment of the eval to original
    if(exception_state==0)
    {
        original->eval=basic_eval(original,W);
        return original->eval;
    }
    if(exception_state==1)
    {
        return 0;
    }
    if(exception_state==2)
    {
        return original->white_move ? INT_MIN: INT_MAX;
    }
    return 50;
}

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

vector<int> assign_depth(const BB* const original, const BB* const wfh, int number_of_new_moves, const int free_depth)
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
        int number_of_new_moves = all_moves(original,wfh);
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

void print_history_to_file(vector<BB> history, string filename)
{
    ofstream file;
    file.open(filename);
    for(int m=0;m<int(history.size());m++)
    {

        for(int i=0;i<12;i++)
        {
            file << history[m].Board[i] << "\n";
        }
        file << history[m].white_move << "\n";
        file << history[m].castle[0][0] << "\n";
        file << history[m].castle[0][1] << "\n";
        file << history[m].castle[1][0] << "\n";
        file << history[m].castle[1][1] << "\n";
        file << history[m].en_passant << "\n";
        file << history[m].eval << "\n";
        file << history[m].number_of_repetitions << "\n";
        file << history[m].halfmoves_since_last_capture_or_pawn_move << "\n";
    }
    file.close();
}

void get_history_from_file(vector<BB> &history, string filename)
{
    ifstream file;
    file.open(filename);
    BB temp;
    do
    {
        
        for(int i=0;i<12;i++)
        {
            file >> temp.Board[i];
        }
        file >> temp.white_move;
        file >> temp.castle[0][0];
        file >> temp.castle[0][1];
        file >> temp.castle[1][0];
        file >> temp.castle[1][1];
        file >> temp.en_passant;
        file >> temp.eval;
        file >> temp.number_of_repetitions;
        file >> temp.halfmoves_since_last_capture_or_pawn_move;
        history.push_back(temp);
    }
    while(!file.eof());
    file.close();
}


#endif

