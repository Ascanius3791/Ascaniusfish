#include"ascaniusfish.hpp"
#include <algorithm>
#include <cstdlib>//for communication with python
#include <thread>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef ascaniusfish_2
#define ascaniusfish_2

using namespace std;
int prunable_moves_total=0;
int pruned_moves=0;
//==================================================================================================================================
int temp_counter=0;
constexpr bool print_mode=0;
constexpr int max_depth=4;
constexpr int max_number_of_threads=4;
int number_of_threads=1;
int count_a=0;
int number_of_mimimax_calls=0;
int number_of_half_moves=0;
BB best_move;

class tournament
{
    public:
    string name = "Tournament";
    int white_wins=0;
    int black_wins=0;
    int draws=0;
    int terminated_games=0;
    void reset()
    {
        white_wins=0;
        black_wins=0;
        draws=0;
        terminated_games=0;
    }
};

tournament my_tournament;

class pruning
{
    public:
    int alpha=INT_MIN;
    int beta=INT_MAX;
    int eval=0;
    vector<int> indices;
    int move_number=0;
    int max_move_number=0;
};
//==================================================================================================================================

void check_for_symmetrical_evaluation(const BB* const original)
{
    BB* temp = new BB;
    copy_BB(original,temp);
    temp->white_move=!temp->white_move;
    if(basic_eval(original)!=basic_eval(temp))
    {
        cout << "The evaluation is not symmetrical" << endl;
        cout << "The evaluation of the original is: " << basic_eval(original) << endl;
        cout << "The evaluation of the temp is: " << basic_eval(temp) << endl;
        cout << "The board is: " << endl;
        print(original->Board);
        cout << "The temp board is: " << endl;
        print(temp->Board);
        exit(0);
    }
    delete temp;
}

void saefty_checks(const BB* const original=0)
{
    if(original)
    {
        check_for_symmetrical_evaluation(original);
    }

}

void initialize_rand()
{
    unsigned int my_int=0;
    unsigned int* ptr=&my_int;   
    unsigned long long int address = reinterpret_cast<unsigned long long int>(ptr);
    srand(address);
}

vector<int> sorting_moves(const BB* const Base, int num, bool WM, WEIGHTS W = WEIGHTS_OG)//returns 0, till end-start-1 ,ordered!
{
    // Create an array of indices [start, end)
    vector<int> indices(num);
    for (int i = 0; i < num; ++i) {
        indices[i] = i;
    }

    // Sort the indices Based on sorting_eval function
    if (WM) 
    {
        sort(indices.begin(), indices.end(), [&Base, &W](int a, int b) 
        {
            return sorting_eval(Base+a, W) > sorting_eval(Base+b, W);
        });
    } 
    else 
    {
        sort(indices.begin(), indices.end(), [&Base, &W](int a, int b) {
            return sorting_eval(Base+a, W) < sorting_eval(Base+b, W);
        });
    }
    return indices;
}

int minimax_saefty_copy(const BB*const original ,BB* const wfh ,int depth, WEIGHTS W= WEIGHTS_OG,int alpha = INT_MIN, int beta = INT_MAX,lookup_table* const table=NULL)
{
    //this checks for exceptions it is faster, than to combine them together, since if there is an exception we dont need to call the eval func(which ofc calls exception eval a second time)
    if(tactical_potential(original->Board)>1000)
    {
       // depth++;
        //cout << "The depth was increased to: " << depth << endl;
    }

    
    //depth++;
    if(table)
    if(table->is_retrivable_eval(original,depth))
    return original->eval;
    if(depth==0)
    {   int evaluation=eval(original,W);
        //if(table)//include this in the table, only if it turns out, that looking up if faster than evaluating
        //table->insert(*original,0);
        return evaluation;
    }
    int exception_state=exception_eval(original);
    if(exception_state==1)
    {
        if(table)
        table->insert(*original,depth);
        return 0;
    }
    if(exception_state==2)
    {
        original->eval=original->white_move ? INT_MIN : INT_MAX;
        if(table)
        table->insert(*original,depth);
        return original->eval;
    }
    vector<int> indices;
    int number_of_new_moves = all_moves(original,wfh);
    indices = sorting_moves(wfh,number_of_new_moves,original->white_move,W);// why on earth would this be slower? its pruning ration is better, by a lot!
    for(int i=0;i<number_of_new_moves;i++)
    {
        //indices.push_back(i);
    }
    prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
   
    int best_eval= original->white_move ? INT_MIN : INT_MAX;
    uint64_t enemy_pieces_original=original->Board[6*original->white_move] | original->Board[6*original->white_move+1] | original->Board[6*original->white_move+2] | original->Board[6*original->white_move+3] | original->Board[6*original->white_move+4] | original->Board[6*original->white_move+5];

    for(int i=0;i<number_of_new_moves;i++)
    {
        uint64_t enemy_pieces_wfh=wfh[indices[i]].Board[6*original->white_move] | wfh[indices[i]].Board[6*original->white_move+1] | wfh[indices[i]].Board[6*original->white_move+2] | wfh[indices[i]].Board[6*original->white_move+3] | wfh[indices[i]].Board[6*original->white_move+4] | wfh[indices[i]].Board[6*original->white_move+5];
//        print(enemy_pieces_original);
//        print(enemy_pieces_wfh);

        int local_depth=depth;
        if(enemy_pieces_original!=enemy_pieces_wfh)
        {
            local_depth++;
            cout << "The depth was increased to: " << local_depth << endl;
            exit(0);
        }
        int eval=minimax_saefty_copy(wfh+indices[i],wfh+number_of_new_moves,local_depth-1,W,alpha,beta,table);
        if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
        eval++;
        if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
        eval--;
        if(original->white_move)
        {
            best_eval=max(best_eval,eval);
            alpha=max(alpha,eval);
        }
        else 
        {
            best_eval=min(best_eval,eval);
            beta=min(beta,eval);
        }
        if(beta<=alpha)
        {
            pruned_moves+=number_of_new_moves-i-1;
            break;
        }
    }
    //if(!table)
    //cout << "The table is not included?" << endl;
    if(table)
    table->insert(*original,depth);
    return best_eval;

   return 0;
}

int minimax(const BB*const original ,BB* const wfh ,int remaining_calls,int depth = 0, WEIGHTS W= WEIGHTS_OG,int alpha = INT_MIN, int beta = INT_MAX,lookup_table* const table=NULL)
{
    if(take_precautions)
    saefty_checks(original);
    number_of_mimimax_calls++;
    if(remaining_calls<0)
    {
        cout << "The remaining_calls in minimax is: " << remaining_calls << endl;
        print(original->Board);
        exit(0);
    }
    
    if(table)
    if(table->is_retrivable_eval(original,remaining_calls))
    return original->eval;
    if(remaining_calls==0)
    
    {   
        //cout << "The remaining_calls is 0, so the rest can be skipped" << endl;
        int evaluation=eval(original,W);
        //if(table)//include this in the table, only if it turns out, that looking up is faster than evaluating
        //table->insert(*original,0);
        return evaluation;
    }
    int exception_state=exception_eval(original);
    if(exception_state==1)
    {
        if(table)
        table->insert(*original,remaining_calls);
        return 0;
    }
    if(exception_state==2)
    {
        original->eval=original->white_move ? INT_MIN : INT_MAX;
        if(table)
        table->insert(*original,remaining_calls);
        return original->eval;
    }
    int number_of_new_moves = all_moves(original,wfh);
    vector<int> indices = sorting_moves(wfh,number_of_new_moves,original->white_move,W);// why on earth would this be slower? its pruning ration is better, by a lot!
    prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
   
    int best_eval= original->white_move ? INT_MIN : INT_MAX;
    vector<int> local_remaining_calls = assign_depth(original,wfh,number_of_new_moves,remaining_calls);//beware, this assigns to the unordered values, ofc it then has to be accessed using indices[i]
    
    for(int i=0;i<number_of_new_moves;i++)
    {        
        int eval= minimax(wfh+indices[i],wfh+number_of_new_moves,local_remaining_calls[indices[i]],depth+1,W,alpha,beta,table);
        /*
        print(wfh[indices[i]].Board); cout << "The eval is: " << eval << endl;
        cout << "positional_eval: " << positional_eval(wfh+indices[i],W) << endl;
        cout << "King_safety W: " << king_safety_of_colour(wfh[indices[i]].Board,1,W) << endl;
        cout << "King_safety B: " << king_safety_of_colour(wfh[indices[i]].Board,0,W) << endl;
        cout << "remaining_calls/avaliable_remaining_calls: " << local_remaining_calls[indices[i]]/float(remaining_calls) << endl;
        cin.get();
        */
        if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
        eval++;
        if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
        eval--;
        if(original->white_move)
        {
            best_eval=max(best_eval,eval);
            alpha=max(alpha,eval);
           // if(eval==best_eval)
            //best_move_index=indices[i];
        }
        else 
        {
            best_eval=min(best_eval,eval);
            beta=min(beta,eval);
          //  if(eval==best_eval)
            //best_move_index=indices[i];
        }
        if(beta<=alpha)
        {
            pruned_moves+=number_of_new_moves-i-1;
            break;
        }
        if(0)
        if(original->white_move && eval+depth==INT_MAX || !original->white_move && eval-depth==INT_MIN)
        {
            //-- or ++ might be necessary
            cout << "Evaluation stopped, because of quickes possible mate is already found" << endl;
            print(original->Board);
            interpret_eval(eval);
            exit(0);
            break;
        }
        
    }
   // cout << "The best move is: " << get_move(original,wfh+best_move_index) << " with eval: " << best_eval << endl;
   // cin.get();
    //if(!table)
    //cout << "The table is not included?" << endl;
    if(table)
    table->insert(*original,remaining_calls);
    return best_eval;

   return 0;
}

/*
void line_saefty_copy(BB* original ,BB* wfh ,int depth , WEIGHTS W= WEIGHTS_OG, int alpha = INT_MIN, int beta = INT_MAX)
{ 
    castling_rights(original);
    do
    {
        alpha=INT_MIN;//those are just a quick fixes, i do not understand why they are needed, since in theory alpha= beta, also, adjusting for the mate being now one more eralier, that teh alpha and beta from the last iteration, changed nothing
        beta=INT_MAX;
        int best_move_index=0;
        int number_of_new_moves = all_moves(original,wfh);
        vector<int> indices = sorting_moves(wfh,number_of_new_moves,original->white_move,W);
        
        prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
        
        int best_eval= original->white_move ? INT_MIN : INT_MAX;
        for(int i=0;i<number_of_new_moves;i++)
        {
            int eval=minimax(wfh+indices[i],wfh+number_of_new_moves,depth-1,W,alpha,beta);
            if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
            eval++;
            if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
            eval--;
            if(original->white_move)
            {
                if(eval>best_eval)
                best_move_index=indices[i];
                best_eval=max(best_eval,eval);
                alpha=max(alpha,eval);
            }
            else 
            {
                if(eval<best_eval)
                best_move_index=indices[i];
                best_eval=min(best_eval,eval);
                beta=min(beta,eval);
            }
            if(beta<=alpha)
            {
                pruned_moves+=number_of_new_moves-i-1;
                break;
            }
        }
            print((*original).Board);
            if(take_history)
            history.push_back(*original);
            interpret_eval(best_eval);
            copy_BB(wfh+best_move_index,original);
            depth--;
                

            if(depth==0 || exception_eval(original))
            {
                print((*original).Board);
                if(exception_eval(original))
                {
                    if(original->white_move)
                    cout << "\nCheckmate, Black wins!" << endl;
                    else
                    cout << "\nCheckmate, White wins!" << endl;
                    return;
                }
                interpret_eval(best_eval);
                return;
            }
    } while (1);
    

}
*/
void line(const BB* original ,BB* wfh ,int remaning_calls ,int depth = 0, WEIGHTS W= WEIGHTS_OG, int alpha = INT_MIN, int beta = INT_MAX)
{ 
    BB temp = *original;
    BB* original_temp=&temp;
    castling_rights(original_temp);
    do
    {
        alpha=INT_MIN;//those are just a quick fixes, i do not understand why they are needed, since in theory alpha= beta, also, adjusting for the mate being now one more eralier, that teh alpha and beta from the last iteration, changed nothing
        beta=INT_MAX;
        int best_move_index=0;
        int number_of_new_moves = all_moves(original_temp,wfh);
        vector<int> indices = sorting_moves(wfh,number_of_new_moves,original_temp->white_move,W);
        
        prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.

        int best_eval= original_temp->white_move ? INT_MIN : INT_MAX;

        vector<int> local_depth = assign_depth(original_temp,wfh,number_of_new_moves,remaning_calls);
        //print((*original_temp).Board);
        //cout << "The remaning_calls is: " << remaning_calls << "and splits a folows" << endl;
        for(int i=0;i<number_of_new_moves;i++)
        {
            int eval=minimax(wfh+indices[i],wfh+number_of_new_moves,local_depth[indices[i]],depth,W,alpha,beta);
            
           // cout << "For move: " << get_move(original_temp,wfh+indices[i]) << " local_remaning_calls: " << local_remaning_calls << "the eval ist" << eval << endl;
            if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
            eval++;
            if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
            eval--;
            if(original_temp->white_move)
            {
                if(eval>best_eval)
                best_move_index=indices[i];
                best_eval=max(best_eval,eval);
                alpha=max(alpha,eval);
            }
            else 
            {
                if(eval<best_eval)
                best_move_index=indices[i];
                best_eval=min(best_eval,eval);
                beta=min(beta,eval);
            }
            if(beta<=alpha)
            {
                pruned_moves+=number_of_new_moves-i-1;
                break;
            }
        }
            
            
            print((*original_temp).Board);
            interpret_eval(best_eval);
            copy_BB(wfh+best_move_index,original_temp);
            remaning_calls=local_depth[best_move_index];
            //remaning_calls--;
                
            if(remaning_calls==0 || exception_eval(original_temp))
            {
                print((*original_temp).Board);
                if(exception_eval(original_temp))
                {
                    if(original_temp->white_move)
                    cout << "\nCheckmate, Black wins!" << endl;
                    else
                    cout << "\nCheckmate, White wins!" << endl;
                    return;
                }
                interpret_eval(best_eval);
                return;
            }
    } while (1);
    

}


void checkmating_line(BB* original, bool mate_for_white, BB* wfh, int remaining_calls,int depth, WEIGHTS W=WEIGHTS_OG)

{
    castling_rights(original);
    if(mate_for_white)
    {
        line(original,wfh,remaining_calls,depth,W,INT_MAX-max_mating_seq,INT_MAX);
    }
    else
    {
        line(original,wfh,remaining_calls,depth,W,INT_MIN,INT_MIN+max_mating_seq);
    }
}

bool is_legit_input(char file, char rank)
{
    if(rank == '1' || rank == '2' || rank == '3' || rank == '4' || rank == '5' || rank == '6' || rank == '7' || rank == '8')
    if(file == 'a' || file == 'b' || file == 'c' || file == 'd' || file == 'e' || file == 'f' || file == 'g' || file == 'h')
    return true;
    
    return false;
}

int result(const BB* const original)//0=game on 1=white wins -1=black wins 2=draw
{
    int exception_state=exception_eval(original);//0=no_exception 1=stalmate 2=checkmate
    
    if(exception_state==0)
    return 0;//game on
    cout << "Thee war wa result: " << exception_state << endl;
    if(exception_state==1)
    return 2;//draw

    if(original->white_move)
    if(exception_state==2)
    return -1;//black wins
    if(!original->white_move)
    if(exception_state==2)
    return 1;//white wins

    cout << "Error there is no result" << endl;
    exit(1);
    return 50;
}

class PP //play parameters
{
    public:
    BB* original=0;
    BB* wfh=0;
    lookup_table* table=0;
    int depth=4;
    bool colour=1;//which colour do you play?
    bool is_human_play=1;
    bool is_pretty_print=1;
    bool is_starting_pos_by_force=0;
    bool is_supposed_to_print_PGN=1;
    bool is_supposed_to_give_out_move=1;
    bool show_eval=0;
    WEIGHTS W=WEIGHTS_OG;/// weights for bith colours
    WEIGHTS W_white=WEIGHTS_OG;
    WEIGHTS W_black=WEIGHTS_OG;
    FILE* pipe=0;
    string name_of_python_script="display_board.py";
    chrono::milliseconds delay=chrono::milliseconds(0);
    bool print_Board=1;
    int max_game_lengh=100;
    string file_original ="weights.txt";
    int Number_of_games =10;
    int learning_rate=5;
    int probability_of_change=10;
};

string read_from_last_move(const bool col)
{
    char colour = col ? 'w' : 'b';
    
    string last_move="";
    while (last_move[5]!=colour || last_move.size()==0)
    {
        ifstream file;
        file.open("last_move.txt");
        this_thread::sleep_for(chrono::milliseconds(200));
        //cout << "Waiting" << endl;
        //cout << last_move << endl;
        getline(file,last_move);
        file.close();
        
    }
        ofstream file;
        file.open("last_move.txt");
        file << "";
        file.close();
    
    
    return last_move;

}

class Play  : public initialize_FEN_to
{
    public :
       

/*
    int improve_weighs(BB*wfh, string file_original, int depth=1,bool show_eval=0, int Number_of_games =10 , bool print_Board =0,string name_of_python_script="", int learning_rate =10 , int max_game_lengh=150, int probability_of_change=100, chrono::milliseconds delay=chrono::milliseconds(0), BB* original=0,lookup_table* table=0)//max_game_lenght is ther beause we currently cannot detect daws by repetition
{
    int counter =0;
    bool OG_plays_white=1;
    int result;
    for(int i=0;i<Number_of_games;i++)
    {
        cout << "\nGame: " << i << endl;
        
        OG_plays_white=!OG_plays_white;
        
        WEIGHTS W_original;
        W_original.read_values_from_file(file_original);

        WEIGHTS W_contender=W_original;
        W_contender.change_values(learning_rate,!OG_plays_white,probability_of_change);
        if(print_Board)
        {
            if(OG_plays_white)
            cout << "Black ist the contender!" << endl;
            else
            cout << "White ist the contender!" << endl;
        }
        if(original==0)
        {
            if(name_of_python_script=="")
        {
            if(OG_plays_white)
            result = play_engine_from_starting_position(wfh,depth,show_eval,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_from_starting_position(wfh,depth,show_eval,W_contender,W_original,print_Board,max_game_lengh,table);
        }
        if(name_of_python_script!="")
        {
            if(OG_plays_white)
            result = play_engine_from_starting_position_with_pretty_print(name_of_python_script,wfh,depth,show_eval,delay,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_from_starting_position_with_pretty_print(name_of_python_script,wfh,depth,show_eval,delay,W_contender,W_original,print_Board,max_game_lengh,table);
        }

        }

        if(original!=0)
        {
            if(name_of_python_script=="")
        {
            if(OG_plays_white)
            result = play_engine(original,wfh,depth,show_eval,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine(original,wfh,depth,show_eval,W_contender,W_original,print_Board,max_game_lengh,table);
        }
        if(name_of_python_script!="")
        {
            if(OG_plays_white)
            result = play_engine_with_pretty_print(original,wfh,name_of_python_script,depth,show_eval,delay,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_with_pretty_print(original,wfh,name_of_python_script,depth,show_eval,delay,W_contender,W_original,print_Board,max_game_lengh,table);
        }

        }
        

        if(result==3)
        my_tournament.terminated_games++;
        if(result==2)
        my_tournament.draws++;
        if(result==1)
        my_tournament.white_wins++;
        if(result==-1)
        my_tournament.black_wins++;
        if(result==0)
        cout << "Error: no result" << endl;//this should be impossible anyway, but better safe than sorry

        if(OG_plays_white)
        if(result==-1)
        {
            cout << "Contender wins as black!" << endl;
            W_contender.print_values_to_file(file_original);
            W_contender.append_weights_to_File();
            counter++;
        }

        if(!OG_plays_white)
        if(result==1)
        {
            cout << "Contender wins as white!" << endl;
            W_contender.print_values_to_file(file_original);
            W_contender.append_weights_to_File();
            counter++;
        }

    }
    
    return counter;
}
    
    int improve_weighs_with_repect_to_W_OG(BB*wfh, string file_original, int depth=1,bool show_eval=0, int Number_of_games =10 , bool print_Board =0,string name_of_python_script="", int learning_rate =10 , int max_game_lengh=150, int probability_of_change=100, chrono::milliseconds delay=chrono::milliseconds(0),const BB*const  original=0,lookup_table* table=0)//max_game_lenght is ther beause we currently cannot detect daws by repetition
    {
        int counter =0;
        bool OG_plays_white=1;
        int result;
        WEIGHTS W_original=WEIGHTS_OG;
        for(int i=0;i<Number_of_games;i++)
        {
            cout << "\nGame: " << i << endl;
            
            OG_plays_white=!OG_plays_white;
            
            

            WEIGHTS W_contender;
            W_contender.read_values_from_file(file_original);
            W_contender.change_values(learning_rate,!OG_plays_white,probability_of_change);
            if(print_Board)
            {
                if(OG_plays_white)
                cout << "Black ist the contender!" << endl;
                else
                cout << "White ist the contender!" << endl;
            }
            if(print_Board)
        {
            if(OG_plays_white)
            cout << "Black ist the contender!" << endl;
            else
            cout << "White ist the contender!" << endl;
        }
        if(original==0)
        {
            if(name_of_python_script=="")
        {
            if(OG_plays_white)
            result = play_engine_from_starting_position(wfh,depth,show_eval,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_from_starting_position(wfh,depth,show_eval,W_contender,W_original,print_Board,max_game_lengh,table);
        }
        if(name_of_python_script!="")
        {
            if(OG_plays_white)
            result = play_engine_from_starting_position_with_pretty_print(name_of_python_script,wfh,depth,show_eval,delay,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_from_starting_position_with_pretty_print(name_of_python_script,wfh,depth,show_eval,delay,W_contender,W_original,print_Board,max_game_lengh,table);
        }

        }

        if(original!=0)
        {
            if(name_of_python_script=="")
        {
            if(OG_plays_white)
            result = play_engine(original,wfh,depth,show_eval,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine(original,wfh,depth,show_eval,W_contender,W_original,print_Board,max_game_lengh,table);
        }
        if(name_of_python_script!="")
        {
            if(OG_plays_white)
            result = play_engine_with_pretty_print(original,wfh,name_of_python_script,depth,show_eval,delay,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_with_pretty_print(original,wfh,name_of_python_script,depth,show_eval,delay,W_contender,W_original,print_Board,max_game_lengh,table);
        }

        }

            
            if(result==3)
            my_tournament.terminated_games++;
            if(result==2)
            my_tournament.draws++;
            if(result==1)
            my_tournament.white_wins++;
            if(result==-1)
            my_tournament.black_wins++;
            if(result==0)
            cout << "Error: no result" << endl;//this should be impossible anyway, but better safe than sorry

            if(OG_plays_white)
            if(result==-1)
            {
                cout << "Contender wins as black!" << endl;
                W_contender.print_values_to_file(file_original);
                W_contender.append_weights_to_File();
                counter++;
            }

            if(!OG_plays_white)
            if(result==1)
            {
                cout << "Contender wins as white!" << endl;
                W_contender.print_values_to_file(file_original);
                W_contender.append_weights_to_File();
                counter++;
            }

        }
        
        return counter;
    }
    */
    
    public:
    PP p;
    //these three should be the only non special(zb checkmating line) play functions
    void human_move(BB *original, BB* wfh, bool pretty_print=0)
    {
            
            ofstream temp_file;
            
            
            
        
            int number_of_new_moves = all_moves(original,wfh);
            bool aligns_with_goal=0;
            int index_of_alignment=0;
            while (!aligns_with_goal)
            {
            if(pretty_print)
            {
                temp_file.open("last_move.txt");
                temp_file << "";
                temp_file.close();
            }
                string move;
                char file;
                char rank;
                int start=0,end=0;
                if(!pretty_print)
                {
                    do
                    {
                        cout<<"\nEnter starting file, rank: ";
                        cin>>file;
                        cin>>rank;
                    } while (!is_legit_input(file, rank));
                    
                    
                    rank--;
                    start=8*(rank-'0')+(file-'a');
                    
                    do
                    {
                        cout<<"Enter ending file, rank: ";
                        cin>>file;
                        cin>>rank;
                    } while (!is_legit_input(file, rank));
                    rank--;
                    end=8*(rank-'0')+(file-'a');
                }
                else
                {
                    move = read_from_last_move(p.colour);
                    
                    cout << "The move was: " << move << endl;
                    start=8*(move[1]-'1')+(move[0]-'a');
                    end=8*(move[3]-'1')+(move[2]-'a');
                    if(original->castle[original->white_move][0])//queen side
                    if(start==4+8*7*(!original->white_move) )// king of relevant colout
                    if(end==2+8*7*(!original->white_move) || end==1+8*7*(!original->white_move) || end==0+8*7*(!original->white_move))//count all moves on the backrank
                    {
                        end=2+8*7*(!original->white_move);
                    }
                    if(original->castle[original->white_move][1])//king side
                    if(start==4+8*7*(!original->white_move) )// king of relevant colout
                    if(end==6+8*7*(!original->white_move) || end==7+8*7*(!original->white_move))//count all moves on the backrank
                    {
                        end=6+8*7*(!original->white_move);
                    }
                }
                
                BB temp = *original;
                int piece_type=0;
                for(int i=0;i<12;i++)
                {
                    if(temp.Board[i] & (1ULL<<start))
                    {
                        piece_type=i;
                        break;
                    }
                }

                temp.Board[piece_type] &= ~(1ULL<<start);
                temp.Board[piece_type] |= (1ULL<<end);
                bool is_promotion=0;
                int promotion_piece=-1;
                if(piece_type==0 && end >= 8*7 || piece_type==6 && end <= 7)
                {
                    is_promotion=1;
                    temp.Board[piece_type] &= ~(1ULL<<end);
                    char promotion;
                    if(!pretty_print)
                    do
                    {
                        cout<<"Enter promotion piece: ";
                        cin>>promotion;
                    } while (promotion!='q' && promotion!='r' && promotion!='b' && promotion!='n');
                    else
                    promotion=move[4];
                    if(promotion=='q' || promotion=='Q')
                    promotion_piece=4+6*(!temp.white_move);
                    if(promotion=='r' || promotion=='R')
                    promotion_piece=1+6*(!temp.white_move);
                    if(promotion=='b' || promotion=='B')
                    promotion_piece=3+6*(!temp.white_move);
                    if(promotion=='n' || promotion=='N')
                    promotion_piece=2+6*(!temp.white_move);
                    temp.Board[promotion_piece] |= (1ULL<<end);
                }
                
                if(!is_promotion)
                for(int i=0;i<number_of_new_moves;i++)
                if(wfh[i].Board[piece_type]==temp.Board[piece_type])
                {
                    index_of_alignment=i;
                    aligns_with_goal=1;
                    break;
                } 
                if(is_promotion)
                for(int i=0;i<number_of_new_moves;i++)
                if(wfh[i].Board[promotion_piece]==temp.Board[promotion_piece])
                {
                    index_of_alignment=i;
                    aligns_with_goal=1;
                    break;
                }
                if(!aligns_with_goal)
                {
                    cout<<"Invalid move. Try again."<<endl;
                    cout <<"The suggested move was: " << get_UCI(original,&temp) << endl;
                    cout << "which corresponds to: " << endl;
                    print(temp.Board);
                }        
            }
            cout << "The move was: " << get_UCI(original,wfh+index_of_alignment) << "aka." << get_move(original,wfh+index_of_alignment) << endl;
            copy_BB(wfh+index_of_alignment,original);
    }
    
    int engine_move(BB* original, BB* wfh, bool pretty_print=0,int depth=1, WEIGHTS W=WEIGHTS_OG,lookup_table* table=0)
    {

        //cin.get();
        cout << "The central pawn presence is for white: " << central_pawn_presence(original,1) << " and for black: " << central_pawn_presence(original,0) << endl;
        cout << "The positional eval is: " << positional_eval(original,W) << endl;
        //cout << "The line is: \n";
        //line(original,wfh,depth,W,INT_MIN,INT_MAX);
        //cout << "The line is over" << endl;
        //cin.get();
        int number_of_new_moves = all_moves(original,wfh);
        if(number_of_new_moves==1)
        {
            copy_BB(wfh,original);
            return INT_MAX/2;
        }
        vector<int> indices = sorting_moves(wfh,number_of_new_moves,original->white_move,W);//descending
        prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
        int alpha=INT_MIN,beta=INT_MAX;
        int best_eval= original->white_move ? INT_MIN : INT_MAX;
        int best_move_index=0;
        //cout << "Engine move calls assign_depth" << endl;
        vector<int> local_depth = assign_depth(original,wfh,number_of_new_moves,depth);
        for(int i=indices[0],k=0;k<number_of_new_moves;k++,i=indices[k])
        {
            int eval=minimax(wfh+i,wfh+number_of_new_moves,local_depth[i],0,W,alpha,beta,table);
           // cout << "The move is: " << get_move(original,wfh+i) << endl;
            //cout << "The eval is: " << eval << endl;
            if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
            eval++;
            if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
            eval--;
            if(original->white_move)
            {
                if(eval>best_eval)
                best_move_index=i;

                best_eval=max(best_eval,eval);
                alpha=max(alpha,eval);
            }
            else 
            {
                if(eval<best_eval)
                best_move_index=i;

                best_eval=min(best_eval,eval);
                beta=min(beta,eval);
            }
            if(beta<=alpha)
            {
                pruned_moves+=number_of_new_moves-k-1;
                break;
            }
        }
        original->eval=best_eval;
        //cout << "local_depth: " << local_depth[best_move_index] << endl;
        //cout << "The best move is: " << get_move(original,wfh+best_move_index) << endl;
        //cout << "The best eval is: " << best_eval << endl;
        copy_BB(wfh+best_move_index,original);
        
        return best_eval;
    }

    int nicely_written_play() // 1 means white wins, -1 means black wins, 2 means stalemate /draw, 3 means terminated game
    {
        int game_result=0;
        int game_lenght=0;
        int current_eval=0;
        
        BB temp;
        BB store_original;
        if(!p.is_starting_pos_by_force)
        temp=*(p.original);
        else
        initialize_FEN_to::Standartboard(temp.Board);

        
        BB* original=&temp;
        castling_rights(original);
        if(p.is_pretty_print)
        p.pipe = open_python_script(p.name_of_python_script,get_FEN(*original));
        if(take_history)
        history.push_back(*original);
        while(game_lenght++<p.max_game_lengh || p.is_human_play)
        {
            number_of_half_moves++;
            int number_of_new_moves = all_moves(original,p.wfh+200);
            print(original->Board);
            cout << endl << get_FEN(*original) << endl;
            //print(attacks_by_col(original->Board,original->white_move));
            //print_history_to_file(history,"game.txt");
            //cout << "Tactiacl potential: " << tactical_potential(original->Board) << endl;
            //cin.get();
            //cout << "The line is: \n";
            //cout <<"The depth is: " << p.depth << endl;
            //cout << "The line at depth " << p.depth << " is: \n";
            //line(original,p.wfh,p.depth,p.W,INT_MIN,INT_MAX);
            //cout << "The line is over" << endl;
            //cin.get();
            //cout << "Piece activity_eval: " << piece_activity_eval(original) << endl;
            bool WM=original->white_move;
            if(!(p.colour==WM && p.is_human_play)&& p.show_eval)//if its an engine move
            interpret_eval(current_eval);
            
            
            store_original=*original;
            
            
            if(p.colour==WM && p.is_human_play)
            {
                if(!p.is_pretty_print)
                human_move(original,p.wfh,p.show_eval);
                else
                human_move(original,p.wfh,p.show_eval);
            }
            else if(p.colour!=WM && p.is_human_play)
            {
                current_eval = engine_move(original,p.wfh,p.show_eval,p.depth,p.W,p.table);
            }
            else if(WM)
            {
                current_eval = engine_move(original,p.wfh,p.show_eval,p.depth,p.W_white,p.table);
            }
            else if(!WM)
            {
                current_eval = engine_move(original,p.wfh,p.show_eval,p.depth,p.W_black,p.table);
            }
            if(p.is_supposed_to_give_out_move)
            cout << game_lenght  << ". "<< get_move(&store_original,original) << endl;
            if(!p.is_pretty_print&&p.print_Board)
            print(original->Board);
            else if(p.print_Board)
            write_to_python_script(p.pipe,get_UCI(&store_original,original));
            
            //print(original->Board);
            if(take_history)
            history.push_back(*original);
            game_result=result(original);
            if(game_result)
            break;
        }

        
        if(p.table)
        {
            p.table->there_are_doubles();
        }
        if(p.is_supposed_to_print_PGN)
        cout << "\nPGN\n" << get_PGN(history,get_FEN(*p.original)) << endl;
        if(p.is_pretty_print)
        close_python_script(p.pipe);
        if(game_result==1)
        {
            cout << "White wins!" << endl;
            return 1;
        }
        if(game_result==-1)
        {
            cout << "Black wins!" << endl;
            return -1;
        }
        if(game_result==2)
        {
            cout << "Stalemate!" << endl;
            return 2;
        }
        return 3;

    }

    int improve_weighs(WEIGHTS* Weight_to_play_against=0)//max_game_lenght is there beause we currently cannot detect daws by repetition
{
    int counter =0;
    bool OG_plays_white=1;
    int result;
    p.is_supposed_to_give_out_move=0;
    p.is_supposed_to_print_PGN=0;

    for(int i=0;i<p.Number_of_games;i++)
    {
        history.clear();
        cout << "\nGame: " << i << endl;
        
        OG_plays_white=!OG_plays_white;
        
        WEIGHTS W_original;
        WEIGHTS W_contender;
        if(!Weight_to_play_against)
        {
            W_original.read_values_from_file(p.file_original);
            W_contender=W_original;
            W_contender.change_values(p.learning_rate,!OG_plays_white,p.probability_of_change);
        }
        else
        {
            W_original=*Weight_to_play_against;
            W_contender.read_values_from_file(p.file_original);
            W_contender.change_values(p.learning_rate,!OG_plays_white,p.probability_of_change);

        }
        WEIGHTS W_white,W_black;
        if(p.print_Board)
        {
            if(OG_plays_white)
            cout << "Black ist the contender!" << endl;
            else
            cout << "White ist the contender!" << endl;
        }
        
        if(OG_plays_white)
        {
            p.W_white=W_original;
            p.W_black=W_contender;
        }
        else
        {
            p.W_white=W_contender;
            p.W_black=W_original;
        }
        p.is_human_play=0;
        
        result=nicely_written_play();

        

        if(result==3)
        my_tournament.terminated_games++;
        if(result==2)
        my_tournament.draws++;
        if(result==1)
        my_tournament.white_wins++;
        if(result==-1)
        my_tournament.black_wins++;
        if(result==0)
        cout << "Error: no result" << endl;//this should be impossible anyway, but better safe than sorry

        if(OG_plays_white)
        if(result==-1)
        {
            cout << "Contender wins as black!" << endl;
            W_contender.print_values_to_file(p.file_original);
            W_contender.append_weights_to_File();
            counter++;
        }

        if(!OG_plays_white)
        if(result==1)
        {
            cout << "Contender wins as white!" << endl;
            W_contender.print_values_to_file(p.file_original);
            W_contender.append_weights_to_File();
            counter++;
        }

    }
    
    return counter;
}
    
};

class PRESENT
{
    private:
    
    PP p;
    BB original;
    BB* wfh;
    void welcome_message()
    {
        cout << "Welcome to Ascaniusfish!\n\n";
        top://my first goto statment, just for fun
        cout << "Do you want to play (p), watch (w) or read more about the project (r)?\n";
        char choice=' ';
        while(choice!='p' && choice!='r' && choice!='w')
        cin >> choice;
        if(choice=='p')
        p.is_human_play=1;
        else
        p.is_human_play=0;
        switch (choice)
        {
        case 'r':
        {
            cout << "Ascaniusfish is (currently) a determinitic chess engine-meaning the same conditions result always in the same outcome.\n";
            cout << "It uses a depth first search algorithm with alpha-beta pruning and a will soon incorporate quiescence search.\n";
            cout << "All feedback is appreciated, and thanks for playing!\n\n";
            cout << "The algorytm works best, if you choose an even depth(you may try to figure out why). The seach time grows exponentially with the depth, with (a roughly estimated) base 10-15.\n";
            cout << "Depth 2 will result in an basically intantanious move, while depth 4 will take a 2-3 seconds.\n";
            cout << "Depth 6 will take at times minutes, details will depend on your hardware, and on the progress of the game.\n";
            cout << "At depth 4 the engine has currenly a roughly approximatet elo of 1100, othe depths are insuficcently tested.\n";
            cout << "After the game you should close the python window, to get a pgn of the game.\n";
            cout << "You will figure out the rest on the fly, have fun!\n";
            goto top;
        }
        case 'p':
        {
            cout << "Do you want to play as white (w) or black (b)?\n";
            char col=' ';
            while(col!='w' && col!='b')
            cin >> col;
            if(col=='w')
            p.colour=1;
            else
            p.colour=0;
        } 
        case 'w':
        {
            cout << "Do you want to see the evaluation of the engine? (y/n)\n";
            char y=' ';
            while(y!='y' && y!='n')
            cin >> y;
            p.show_eval=y=='y';
            cout << "What depth do you want? (i reccomend 4, but if you are very impatient 2 is also ok)\n";
            cin >> p.depth;
            
            p.original=&original;
            wfh=new BB[80*p.depth];
            p.wfh=wfh;
            cout << "Do you want to start from a specific position (y/n)?\n";
            char y2=' ';
            cin >> y2;
            while(y2!='y' && y2!='n')
            cin >> y2;
            
            initialize_FEN_to::Standartboard(original.Board);
            if(y2=='y')
            {
            cout << "Please enter the FEN of the position you want to start from:\n";

            string FEN;
            cin.ignore(numeric_limits<std::streamsize>::max(), '\n');
            getline(cin,FEN);
            cout << "The FEN you entered is: " << FEN << endl;
            FEN_to_BB(FEN,&original);
            print(original.Board);
            }
             
        }
        }

    }

    public:
    void play()
    {
        welcome_message();
        
        p.max_game_lengh=150;
        Play play;
        p.table= new lookup_table;
        play.p=p;
        string continue_playing="y";
        do
        {
            play.nicely_written_play();
            p.table->get_number_of_entrys();
            cout << "The number of insertions: " << p.table->number_of_inserions << endl;
            cout << "The number of number_of_succ_readouts: " << p.table->number_of_succ_readouts << endl;
            cout << "THe total number of_readouts: " << p.table->number_of_attemted_readouts << endl;
            cout << "Do you want to play again? (y/n)\n";
            do{
                cin >> continue_playing;
            } while (continue_playing!="y" && continue_playing!="n");
        } while (continue_playing=="y");
        
        delete[] p.wfh;
        delete p.table;
        
        
    }
};

double round_to_percentage(double number)
{
    double return_value= round(number*100)/100;
    if(return_value<100 && return_value>99.99)
    return 100;
    return return_value;
}

double win_chance(int eval)
{
    return 50 + 50 * (2 / (1 + exp(-0.00368208 * eval)) - 1);
}



double accuracy(int eval_bevore, int eval_after)
{

    if(eval_after>eval_bevore)
    return 100;//if your move was better than the top move recommended by the engine you deserve at least 100% accuracy
    double return_value = 103.1668 * exp(-0.04354 * (win_chance(eval_bevore) - win_chance(eval_after))) - 3.1669;
    return max(0.0,round_to_percentage(return_value));
}

double* evaluate_game(vector<BB> history, const int remaining_calls, WEIGHTS W=WEIGHTS_OG)
{
    double accuracy_per_move[history.size()-1];
    double score=0;
    double eval_bevore[history.size()-1];
    double eval_after[history.size()-1];
    BB* wfh = new BB[300];
    for(int i=0;i<int(history.size())-1;i++)
    {   
        int corr_f=1-2*!history[i].white_move;
        
        eval_bevore[i]=minimax(&history[i],wfh,remaining_calls,0,W)*corr_f;
        int number_of_new_moves = all_moves(&history[i],wfh);
        if(remaining_calls==0)
        {
            cout << "Error: The game is not over" << endl;
            exit(1);
        }
        vector<int> assigned_depth = assign_depth(&history[i],wfh,number_of_new_moves,remaining_calls);
        int index_of_alignment=-1;
        for(int j=0;j<number_of_new_moves;j++)
        {
            if(are_equal(&history[i+1],&wfh[j]))
            {
                index_of_alignment=j;
                break;
            }
        }
        if(index_of_alignment==-1)
        {
            cout << "Error: The move was not found" << endl;
            exit(1);
        }
        
        
        //eval_after[i]=minimax(&history[i+1],wfh,assigned_depth[index_of_alignment],0,W);
        eval_after[i]=minimax(&history[i+1],wfh,assigned_depth[i],0,W)*corr_f;
        
        if(eval_after[i]<INT_MIN+max_mating_seq)
        eval_after[i]++;
        if(eval_after[i]>INT_MAX-max_mating_seq)
        eval_after[i]--;
        
        
        
        accuracy_per_move[i]=accuracy(eval_bevore[i],eval_after[i]);
        print(history[i].Board);
        cout << "The move was: " << get_move(&history[i],&history[i+1]) << " and the accuracy was: " << accuracy_per_move[i] << "%" << endl;
        
        
       
    }
    int white_moves=0;
    for(int i=0;i<int(history.size())-1;i=i+2)  
    {
        score+=accuracy_per_move[i];
        white_moves++;
    } 
    double* return_value = new double[2];
    return_value[0]=round_to_percentage(score/white_moves);
    int black_moves=0;
    for(int i=1;i<int(history.size())-1;i=i+2)  
    {
        score+=accuracy_per_move[i];
        black_moves++;
    }
    return_value[1]=round_to_percentage(score/(white_moves+black_moves));

    
    return return_value;
}

#endif