// OWNERSHIP=Ascanius
#include"ascaniusfish.hpp"
#include "lib/cuckoo_cycle_table.hpp"
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
constexpr int max_number_of_threads=4;
int number_of_threads=1;
int number_of_mimimax_calls=0;
int number_of_half_moves=0;


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

void initialize_rand()
{
    unsigned int my_int=0;
    unsigned int* ptr=&my_int;   
    unsigned long long int address = reinterpret_cast<unsigned long long int>(ptr);
    srand(address);
}

vector<int> sorting_moves(const BB* const Base, vector<Move> moves, int num, bool WM, const PV_Line* const pv_line =0, int index_of_pv_line_to_compare_against=0, WEIGHTS W = WEIGHTS_OG)//returns 0, till end-start-1 ,ordered!
{
    // Create an array of indices [start, end)
    vector<int> indices(num);
    vector<int> sorting_scores(num);
    for (int i = 0; i < num; ++i) {
        indices[i] = i;
        sorting_scores[i] = sorting_eval(Base + i, W);
    }

    sort(indices.begin(), indices.end(), [&sorting_scores, WM](int a, int b)
    {
        return WM
            ? sorting_scores[a] > sorting_scores[b]
            : sorting_scores[a] < sorting_scores[b];
    });
    if (pv_line && pv_line->current_lenght !=0)
    {
        Move pv_move = pv_line->moves[index_of_pv_line_to_compare_against];

        for (int i = 0; i < num; ++i)
        {
            if (moves[indices[i]] == pv_move)
            {
                rotate(indices.begin(), indices.begin() + i, indices.begin() + i + 1);
                break;
            }
        }
    }
    //if pv_line is provided set that move on top
    return indices;
}

PV_Line minimax(const BB*const original ,BB* const wfh ,int depth = 0, WEIGHTS W= WEIGHTS_OG,int alpha = INT_MIN, int beta = INT_MAX,lookup_table* const table=NULL, BB* const path_history=nullptr, int ply=0, const CuckooCycleTable* const cycle_table=nullptr)
{
    if(DEBUG_MODE)
    saefty_checks(original);
    number_of_mimimax_calls++;

    // Repetition/cycle detection - runs BEFORE the TT probe below, since a
    // TT-cached eval for this exact board doesn't know about THIS path's
    // history: if this position is a repeat right now, a stale non-draw TT
    // entry for the same raw board would be wrong here. Neither draw result
    // below is inserted into the TT - unlike the stalemate/checkmate case
    // further down, a repetition-forced draw is PATH-DEPENDENT (the same exact
    // board can be a draw in one line and not in another), so caching it would
    // corrupt a later, unrelated encounter of that board.
    if(path_history && ply<MAX_SEARCH_PLY)
    {
        int window_start = max(0, ply - original->halfmoves_since_last_capture_or_pawn_move);

        //exact repeat: same side to move, so only even ply gaps can match
        for(int i=ply-2; i>=window_start; i-=2)
        {
            if(are_equal(&path_history[i], original))
            {
                PV_Line draw_pv_line = PV_Line(0);
                draw_pv_line.depth = depth;
                draw_pv_line.current_lenght = 0;
                draw_pv_line.bound_type = 0;
                return draw_pv_line;
            }
        }

        //one reversible move from recreating an earlier position: opposite side
        //to move right now, so only odd ply gaps are candidates. Starts at
        //ply-3, NOT ply-1: ply_gap=1 means "the immediate parent", and undoing
        //whatever move was just played to reach `original` is ALWAYS available
        //for any quiet move - that's not a cycle, it's just what "reversible"
        //means, and treating it as one would score almost every quiet move as
        //an instant draw (this was a real bug - it made the engine play
        //nearly at random, favoring whichever move sorting_moves tried first).
        if(cycle_table)
        {
            for(int i=ply-3; i>=window_start; i-=2)
            {
                int found_piece_type;
                Move found_move;
                if(cycle_table->detect_upcoming_cycle(*original, path_history[i], ply-i, found_piece_type, found_move))
                {
                    PV_Line draw_pv_line = PV_Line(0);
                    draw_pv_line.depth = depth;
                    draw_pv_line.current_lenght = 0;
                    draw_pv_line.bound_type = 0;
                    return draw_pv_line;
                }
            }
        }

        path_history[ply] = *original;
    }

    PV_Line tt_hint;
    bool is_tt_hint_found=0;
    if(table)
        {
            TT_readout readout = table->is_retrivable_eval(original, depth);

            if(readout.is_found)
            {
                if(depth<=readout.pv_line.depth && readout.pv_line.current_lenght>0)
                {
                    if(readout.pv_line.bound_type==0)//exact
                    return readout.pv_line;
                    else if(depth==readout.pv_line.depth)//only on exact depth alpha and beta can be updated
                    {
                        //update alpha and beta based on the bound type
                        if(readout.pv_line.bound_type == -1) // lower bound
                        alpha = max(alpha, readout.pv_line.eval);
                        else if(readout.pv_line.bound_type == 1) // upper bound
                        beta = min(beta, readout.pv_line.eval);
                        if(alpha >= beta)
                        {
                            // Prune the search
                            return readout.pv_line;
                        }
                    }
                }
                tt_hint=readout.pv_line;
                is_tt_hint_found=1;
            }
        }
        
    if(depth==0)
    {   
        int tactical_pot = tactical_potential(original->Board,W);
        
        const int tactical_potential_threshold = INT_MAX;//effectivly deactivates this
        if(tactical_pot<tactical_potential_threshold)
        {
            int evaluation=eval(original,W);
            //if(table)//include this in the table, only if it turns out, that looking up is faster than evaluating
            //table->insert(*original,0);
            PV_Line returned_line = PV_Line(evaluation);
            returned_line.depth=depth;
            returned_line.current_lenght=0;
            returned_line.bound_type = 0; // exact evaluation
            return returned_line;
        }
        else
        {
            cout << "Tactical potential is high: " << tactical_pot << ", proceeding with deeper search." << endl;
            //print if its a white or black move
            if(original->white_move)
            printf("White to move\n");
            else
            printf("Black to move\n");
            print(original->Board);
            depth++;
        }
    }
    
    int exception_state=exception_eval(original);
    if(exception_state==1||exception_state==2)
        {
            int best_eval;
            if(exception_state==1)
            best_eval=0;
            else if(exception_state==2)
            {
                best_eval=original->white_move ? INT_MIN : INT_MAX;
            }
            PV_Line exception_pv_line = PV_Line(best_eval);
            exception_pv_line.depth=depth;
            exception_pv_line.current_lenght=0;
            exception_pv_line.bound_type = 0; // exact evaluation
            if(table)
            {
                TT_entry entry;
                entry.zobrist_hash=original->zobrist_hash;
                entry.board = *original;
                entry.initialized = true;
                entry.pv_line = exception_pv_line;
                entry.pv_line.bound_type = 0;
                table->insert(entry);
            }
            return exception_pv_line;
        }
    
    auto result = all_moves(original,wfh);
    int number_of_new_moves = std::get<0>(result);
    vector<Move> moves = std::get<1>(result);
    vector<int> indices = sorting_moves(wfh,moves,number_of_new_moves,original->white_move,&tt_hint,0,W);// why on earth would this be slower? its pruning ration is better, by a lot!
    prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
    Move best_move;
    const int alpha_0 = alpha, beta_0 = beta;
    PV_Line pv_line =PV_Line(original->white_move ? INT_MIN : INT_MAX);//initialize with worst possible value for the player to move
    pv_line.depth=depth;
    for(int i=0;i<number_of_new_moves;i++)
    {    
        int depth_to_use=depth-1;
        if(captures_more_valuable_piece(original,wfh+indices[i],W))
        depth_to_use++;   
        Move move =moves[indices[i]];
        PV_Line candidate_pv_line = minimax(wfh+indices[i],wfh+number_of_new_moves,depth_to_use,W,alpha,beta,table,path_history,ply+1,cycle_table);
        int eval = candidate_pv_line.eval;
        if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
        eval++;
        if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
        eval--;
        candidate_pv_line.eval=eval;
        bool improves_pv;
        if(original->white_move)
        {
            improves_pv = eval>pv_line.eval;
            pv_line.eval=max(pv_line.eval,eval);
            alpha=max(alpha,eval);
        }
        else
        {
            improves_pv = eval<pv_line.eval;
            pv_line.eval=min(pv_line.eval,eval);
            beta=min(beta,eval);
        }
        if(improves_pv)
        {
            pv_line = PV_Line(move,depth,&candidate_pv_line);
        }
        
        if(beta<=alpha)
        {
            pruned_moves+=number_of_new_moves-i-1;
            break;
        }        
    }
    //now correct for fail low
        if(pv_line.eval <= alpha_0)
            pv_line.bound_type = 1;   // upper bound (fail-low)
        else if(pv_line.eval >= beta_0)
            pv_line.bound_type = -1;  // lower bound (fail-high)
        else
            pv_line.bound_type = 0;   // exact
    if(table)
    {
        TT_entry entry;
        entry.board = *original;
        entry.zobrist_hash=original->zobrist_hash;
        entry.pv_line = pv_line;
        entry.initialized = true;
        table->insert(entry);
    }
    return pv_line;
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
    {
        // Draw by threefold repetition: `history` (populated by
        // nicely_written_play()) already has *original as its last entry at
        // this point, so counting how many times this exact position (board +
        // side to move + castling rights + en passant, via the existing
        // are_equal()) appears in it naturally includes "now" as one
        // occurrence. Bounded to the tail since the last capture/pawn move,
        // same as minimax()'s own repetition window - nothing further back
        // could ever repeat. This is the real FIDE threefold rule, not
        // minimax()'s more aggressive search-time draw heuristic (which also
        // treats a single upcoming-cycle as drawish for pruning purposes) -
        // the two are deliberately different: one is the actual game result,
        // the other is a search approximation.
        int window_start = max(0, (int)history.size()-1-original->halfmoves_since_last_capture_or_pawn_move);
        int occurrences = 0;
        for(int i=(int)history.size()-1; i>=window_start; i--)
        {
            if(are_equal(&history[i], original))
            occurrences++;
        }
        if(occurrences>=3)
        {
            cout << "Draw by threefold repetition." << endl;
            return 2;//draw
        }
        return 0;//game on
    }
    cout << "Thee war was result: " << exception_state << endl;
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
            
            
            
            auto result = all_moves(original,wfh);
            int number_of_new_moves = std::get<0>(result);
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

    int engine_move_old(BB* original, BB* wfh, bool pretty_print=0,int depth=1, WEIGHTS W=WEIGHTS_OG,lookup_table* table=0,FILE* pipe=0)
    {
        int alpha=INT_MIN,beta=INT_MAX;
        int best_move_index=0;
        
        bool table_provides_eval=0;
        Move tabulated_move;

        PV_Line pv_line =PV_Line(original->white_move ? INT_MIN : INT_MAX);//initialize with worst possible value for the player to movey
        PV_Line tt_hint;
        bool is_tt_hint_found=0;
        if(table)
        {
            TT_readout readout = table->is_retrivable_eval(original,depth);
            if(readout.is_found)
            {
                if(depth<=readout.pv_line.depth)
                {
                    if(readout.pv_line.bound_type==0 && readout.pv_line.current_lenght>0)//exact
                    {
                        table_provides_eval=1;
                        pv_line=readout.pv_line;
                        pv_line.depth=readout.pv_line.depth;
                        tabulated_move=readout.pv_line.moves[0];
                    }
                    
                    else if(depth==readout.pv_line.depth)//only on exact depth alpha and beta can be updated
                    {
                        //update alpha and beta based on the bound type
                        if(readout.pv_line.bound_type == -1) // lower bound
                        alpha = max(alpha, readout.pv_line.eval);
                        else if(readout.pv_line.bound_type == 1) // upper bound
                        beta = min(beta, readout.pv_line.eval);
                    }
                }
                tt_hint=readout.pv_line;
                is_tt_hint_found=1;
            }
        }
        auto result = all_moves(original,wfh);
        int number_of_new_moves = std::get<0>(result);
        vector<Move> moves = std::get<1>(result);
        if(number_of_new_moves==1)
        {
            copy_BB(wfh,original);
            return INT_MAX/2;
        }
        vector<int> indices = sorting_moves(wfh,moves,number_of_new_moves,original->white_move,&tt_hint,0,W);//descending
        
        if(table_provides_eval)//may need rework
            {
                int i=indices[0];
                if(pipe)
                {
                    const int displayed_length = pv_line.current_lenght;
                    //min(pv_line.depth,pv_line.current_lenght);
                    vector<Move> pv_moves(pv_line.moves,
                                          pv_line.moves + displayed_length);
                    write_to_python_script(pipe, "PV depth=" + to_string(depth) +
                                                " eval=" + to_string(pv_line.eval) +
                                                " " + moves_to_PGN(*original, pv_moves));
                }
                copy_BB(wfh+i,original);
                return pv_line.eval;
            }
        prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
        //cout << "Engine move calls assign_depth" << endl;
        const int alpha_0 = alpha, beta_0 = beta;
        PV_Line best_candidate_PV_line;
        bool best_candidate_PV_line_initialized=0;
        for(int i=indices[0],k=0;k<number_of_new_moves;k++,i=indices[k])
        {
            Move move = moves[indices[k]];
            int depth_to_use=depth-1;
            if(captures_more_valuable_piece(original,wfh+i,W))
            depth_to_use++;
            PV_Line candidate_PV_line;
            for(int j=0;j<=depth_to_use;j++)
            {
                candidate_PV_line = minimax(wfh+i,wfh+number_of_new_moves,j,W,alpha,beta,table);
            }
            int eval = candidate_PV_line.eval;
            if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
            eval++;
            if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
            eval--;
            if(original->white_move)
            {
                if(eval>pv_line.eval)
                {
                    best_move_index=i;
                    pv_line = PV_Line(move,depth,&candidate_PV_line);
                    best_candidate_PV_line = candidate_PV_line;
                    best_candidate_PV_line_initialized=1;
                }
                pv_line.eval=max(pv_line.eval,eval);
                alpha=max(alpha,eval);
            }
            else 
            {
                if(eval<pv_line.eval)
                {
                    best_move_index=i;
                    pv_line = PV_Line(move,depth,&candidate_PV_line);
                    best_candidate_PV_line = candidate_PV_line;
                    best_candidate_PV_line_initialized=1;
                }

                pv_line.eval=min(pv_line.eval,eval);
                beta=min(beta,eval);
            }
            if(beta<=alpha)
            {
                pruned_moves+=number_of_new_moves-k-1;
                break;
            }
        }
        //now correct for fail low
        if(pv_line.eval <= alpha_0)
            pv_line.bound_type = 1;   // upper bound (fail-low)
        else if(pv_line.eval >= beta_0)
            pv_line.bound_type = -1;  // lower bound (fail-high)
        else
            pv_line.bound_type = 0;   // exact
        if(table)
        {
            TT_entry entry;
            entry.board = *original;
            entry.zobrist_hash=original->zobrist_hash;
            entry.pv_line = pv_line;
            entry.initialized=1;
            table->insert(entry);
        }
        if(pipe)
        {
            const int displayed_length = min(pv_line.depth,
                                              pv_line.current_lenght);
            vector<Move> pv_moves(pv_line.moves,
                                  pv_line.moves + displayed_length);
            write_to_python_script(pipe, "PV depth=" + to_string(depth) +
                                        " eval=" + to_string(pv_line.eval) +
                                        " " + moves_to_PGN(*original, pv_moves));
        }
        //now print the engine line
        if(best_candidate_PV_line_initialized)
        cout << "The best candidate PV line was initialized!" << endl;
        else
        cout << "The best candidate PV line was NOT initialized!" << endl;
        cout << "The best candidate PV line is: " << endl;
        for(int i=0;i<best_candidate_PV_line.current_lenght;i++)
        {
            cout << "best_candidate_PV_line.depth: " << best_candidate_PV_line.depth << endl;
            Move move = best_candidate_PV_line.moves[i];
            int starting_rank = move.from/8;
            char starting_file = 'a' + (move.from % 8);
            int ending_rank = move.to/8;
            char ending_file = 'a' + (move.to % 8);
            cout << starting_file << starting_rank+1 << " " << ending_file << ending_rank+1 << std::endl;
        }
        cout << "The engine line is: " << endl;
        
        for(int i=0;i<pv_line.current_lenght;i++)
        {
            cout << "pv_line.depth: " << pv_line.depth << endl;
            Move move = pv_line.moves[i];
            int starting_rank = move.from/8;
            char starting_file = 'a' + (move.from % 8);
            int ending_rank = move.to/8;
            char ending_file = 'a' + (move.to % 8);
            cout << starting_file << starting_rank+1 << " " << ending_file << ending_rank+1 << std::endl;
            
        }
        const int displayed_length = min(pv_line.depth,
                          pv_line.current_lenght);
        vector<Move> pv_moves(pv_line.moves,
                      pv_line.moves + displayed_length);
        cout << "Pretty engine line: " << moves_to_PGN(*original, pv_moves) << std::endl;
        cout << "Evaluation after this move: " << pv_line.eval << std::endl;
        copy_BB(wfh+best_move_index,original);
        return pv_line.eval;
    }

    // Thin wrapper around minimax(): minimax already returns the root PV, so the only
    // thing kept here is the outer iterative-deepening loop, which feeds the lookup
    // table forward from shallow to deep so sorting_moves gets a good PV-move hint at
    // every depth. Everything minimax already does internally (root TT short-circuit,
    // alpha-beta, TT insertion) is not duplicated here.
    int engine_move(BB* original, BB* wfh, bool pretty_print=0,int depth=1, WEIGHTS W=WEIGHTS_OG,lookup_table* table=0,FILE* pipe=0)
    {
        auto result = all_moves(original,wfh);
        int number_of_new_moves = std::get<0>(result);
        vector<Move> moves = std::get<1>(result);
        if(number_of_new_moves==1)
        {
            copy_BB(wfh,original);
            return INT_MAX/2;
        }

        // Repetition/cycle detection context for minimax(): built once (the
        // CuckooCycleTable's population and the path array's default-construction
        // both only happen on the very first call, thanks to `static`) and reseeded
        // from the real game's `history` on every move. Only the tail bounded by
        // halfmoves_since_last_capture_or_pawn_move is copied - nothing further
        // back could ever be part of a repeat, and `history` itself can outgrow
        // MAX_SEARCH_PLY over a long game.
        static const CuckooCycleTable cycle_table;
        static BB path_history[MAX_SEARCH_PLY];
        int seed_count = min({(int)history.size(), original->halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
        for(int i=0;i<seed_count;i++)
        path_history[i] = history[history.size()-seed_count+i];
        int ply = (seed_count>0) ? seed_count-1 : 0;

        PV_Line pv_line;
        for(int d=1;d<=depth;d++)
        {
            pv_line = minimax(original,wfh+number_of_new_moves,d,W,INT_MIN,INT_MAX,table,path_history,ply,&cycle_table);
            if(pipe)
            {
                const int displayed_length = min(pv_line.depth,pv_line.current_lenght);
                vector<Move> pv_moves(pv_line.moves,pv_line.moves+displayed_length);
                write_to_python_script(pipe, "PV depth=" + to_string(d) +
                                            " eval=" + to_string(pv_line.eval) +
                                            " " + moves_to_PGN(*original, pv_moves));
            }
        }

        int best_move_index=0;
        for(int i=0;i<number_of_new_moves;i++)
        {
            if(moves[i]==pv_line.moves[0])
            {
                best_move_index=i;
                break;
            }
        }

        if(pretty_print)
        {
            const int displayed_length = min(pv_line.depth,pv_line.current_lenght);
            vector<Move> pv_moves(pv_line.moves,pv_line.moves+displayed_length);
            cout << "Engine line: " << moves_to_PGN(*original, pv_moves) << std::endl;
            cout << "Evaluation after this move: " << pv_line.eval << std::endl;
        }

        copy_BB(wfh+best_move_index,original);
        return pv_line.eval;
    }

    int engine_move_for_time_testing(BB* original, BB* wfh, bool pretty_print=0,int depth=1, WEIGHTS W=WEIGHTS_OG,lookup_table* table=0,FILE* pipe=0)
    {
        int alpha=INT_MIN,beta=INT_MAX;
        int best_move_index=0;
        
        bool table_provides_eval=0;
        Move tabulated_move;

        PV_Line pv_line =PV_Line(original->white_move ? INT_MIN : INT_MAX);//initialize with worst possible value for the player to movey
        PV_Line tt_hint;
        bool is_tt_hint_found=0;
        if(table)
        {
            TT_readout readout = table->is_retrivable_eval(original,depth);
            if(readout.is_found)
            {
                if(depth<=readout.pv_line.depth)
                {
                    if(readout.pv_line.bound_type==0 && readout.pv_line.current_lenght>0)//exact
                    {
                        table_provides_eval=1;
                        pv_line=readout.pv_line;
                        pv_line.depth=readout.pv_line.depth;
                        tabulated_move=readout.pv_line.moves[0];
                    }
                    
                    else if(depth==readout.pv_line.depth)//only on exact depth alpha and beta can be updated
                    {
                        //update alpha and beta based on the bound type
                        if(readout.pv_line.bound_type == -1) // lower bound
                        alpha = max(alpha, readout.pv_line.eval);
                        else if(readout.pv_line.bound_type == 1) // upper bound
                        beta = min(beta, readout.pv_line.eval);
                    }
                }
                tt_hint=readout.pv_line;
                is_tt_hint_found=1;
            }
        }
        auto result = all_moves(original,wfh);
        int number_of_new_moves = std::get<0>(result);
        vector<Move> moves = std::get<1>(result);
        if(number_of_new_moves==1)
        {
            copy_BB(wfh,original);
            return INT_MAX/2;
        }
        vector<int> indices = sorting_moves(wfh,moves,number_of_new_moves,original->white_move,&tt_hint,0,W);//descending
        
        if(table_provides_eval)
            {
                int i=indices[0];
                if(pipe)
                {
                    const int displayed_length = pv_line.current_lenght;
                    //min(pv_line.depth,pv_line.current_lenght);
                    vector<Move> pv_moves(pv_line.moves,
                                          pv_line.moves + displayed_length);
                    write_to_python_script(pipe, "PV depth=" + to_string(depth) +
                                                " eval=" + to_string(pv_line.eval) +
                                                " " + moves_to_PGN(*original, pv_moves));
                }
                copy_BB(wfh+i,original);
                return pv_line.eval;
            }
        prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
        //cout << "Engine move calls assign_depth" << endl;
        const int alpha_0 = alpha, beta_0 = beta;
        PV_Line best_candidate_PV_line;
        bool best_candidate_PV_line_initialized=0;
        for(int i=indices[0],k=0;k<number_of_new_moves;k++,i=indices[k])
        {
            Move move = moves[indices[k]];
            int depth_to_use=depth-1;
            if(captures_more_valuable_piece(original,wfh+i,W))
            depth_to_use++;
            PV_Line candidate_PV_line;
            // for(int j=depth_to_use;j<=depth_to_use;j++)
            // candidate_PV_line = minimax(wfh+i,wfh+number_of_new_moves,j,W,alpha,beta,table);
            printf("Evaluating move %d/%d: %s\n", k + 1, number_of_new_moves, get_UCI(original, wfh + i).c_str());
            print(wfh[i].Board);
            //time the minimax calls with and without lookup table
            auto start_time = chrono::high_resolution_clock::now();
            PV_Line pv_line_without_lookup_table = minimax(wfh+i,wfh+number_of_new_moves,depth_to_use,W,alpha,beta,NULL);
            auto end_time = chrono::high_resolution_clock::now();
            auto duration_without_lookup = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
            auto start_time_with_lookup = chrono::high_resolution_clock::now();
            PV_Line pv_line_with_lookup_table = minimax(wfh+i,wfh+number_of_new_moves,depth_to_use,W,alpha,beta,table);
            auto end_time_with_lookup = chrono::high_resolution_clock::now();
            auto duration_with_lookup = chrono::duration_cast<chrono::milliseconds>(end_time_with_lookup - start_time_with_lookup).count();
            
            //now delte the contents of the lookup table to see the difference
            table->reset();
            auto start_time_with_lookup_v2 = chrono::high_resolution_clock::now();
            PV_Line pv_line_with_lookup_table_v2 = minimax(wfh+i,wfh+number_of_new_moves,depth_to_use,W,alpha,beta,table);
            auto end_time_with_lookup_v2 = chrono::high_resolution_clock::now();
            auto duration_with_lookup_v2 = chrono::duration_cast<chrono::milliseconds>(end_time_with_lookup_v2 - start_time_with_lookup_v2).count();
            
            //now a third time first delete the tables contents, then call minimax with iterativly increasing depth
            table->reset();
            auto start_time_with_lookup_v3 = chrono::high_resolution_clock::now();
            PV_Line pv_line_with_lookup_table_v3;
            for(int j=0;j<=depth_to_use;j++)
            {
                pv_line_with_lookup_table_v3 = minimax(wfh+i,wfh+number_of_new_moves,j,W,alpha,beta,table);
            }
            auto end_time_with_lookup_v3 = chrono::high_resolution_clock::now();
            auto duration_with_lookup_v3 = chrono::duration_cast<chrono::milliseconds>(end_time_with_lookup_v3 - start_time_with_lookup_v3).count();
            cout << "Time taken without lookup table: " << duration_without_lookup << " ms" << endl;
            cout << "Time taken with lookup table (with lookup table filled from previous moves): " << duration_with_lookup << " ms" << endl;
            cout << "Time taken with lookup table v2 (no precomputed table): " << duration_with_lookup_v2 << " ms" << endl;
            cout << "Time taken with lookup table v3 (iterative deepening): " << duration_with_lookup_v3 << " ms" << endl;


            cout << "Eval and bound type without lookup table: " << pv_line_without_lookup_table.eval << ", bound type: " << pv_line_without_lookup_table.bound_type << endl;
            cout << "Eval and bound type with lookup table: " << pv_line_with_lookup_table.eval << ", bound type: " << pv_line_with_lookup_table.bound_type << endl;
            cout << "Eval and bound type with lookup table v2: " << pv_line_with_lookup_table_v2.eval << ", bound type: " << pv_line_with_lookup_table_v2.bound_type << endl;
            cout << "Eval and bound type with lookup table v3: " << pv_line_with_lookup_table_v3.eval << ", bound type: " << pv_line_with_lookup_table_v3.bound_type << endl;
            cin.get(); // Wait for user input before proceeding
            candidate_PV_line = pv_line_with_lookup_table;
            int eval = candidate_PV_line.eval;
            if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
            eval++;
            if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
            eval--;
            if(original->white_move)
            {
                if(eval>pv_line.eval)
                {
                    best_move_index=i;
                    pv_line = PV_Line(move,depth,&candidate_PV_line);
                    best_candidate_PV_line = candidate_PV_line;
                    best_candidate_PV_line_initialized=1;
                }
                pv_line.eval=max(pv_line.eval,eval);
                alpha=max(alpha,eval);
            }
            else 
            {
                if(eval<pv_line.eval)
                {
                    best_move_index=i;
                    pv_line = PV_Line(move,depth,&candidate_PV_line);
                    best_candidate_PV_line = candidate_PV_line;
                    best_candidate_PV_line_initialized=1;
                }

                pv_line.eval=min(pv_line.eval,eval);
                beta=min(beta,eval);
            }
            if(beta<=alpha)
            {
                pruned_moves+=number_of_new_moves-k-1;
                break;
            }
        }
        //now correct for fail low
        if(pv_line.eval <= alpha_0)
            pv_line.bound_type = 1;   // upper bound (fail-low)
        else if(pv_line.eval >= beta_0)
            pv_line.bound_type = -1;  // lower bound (fail-high)
        else
            pv_line.bound_type = 0;   // exact
        if(table)
        {
            TT_entry entry;
            entry.board = *original;
            entry.zobrist_hash=original->zobrist_hash;
            entry.initialized = true;
            entry.pv_line = pv_line;
            table->insert(entry);
        }
        if(pipe)
        {
            const int displayed_length = min(pv_line.depth,
                                              pv_line.current_lenght);
            vector<Move> pv_moves(pv_line.moves,
                                  pv_line.moves + displayed_length);
            write_to_python_script(pipe, "PV depth=" + to_string(depth) +
                                        " eval=" + to_string(pv_line.eval) +
                                        " " + moves_to_PGN(*original, pv_moves));
        }
        //now print the engine line
        if(best_candidate_PV_line_initialized)
        cout << "The best candidate PV line was initialized!" << endl;
        else
        cout << "The best candidate PV line was NOT initialized!" << endl;
        cout << "The best candidate PV line is: " << endl;
        for(int i=0;i<best_candidate_PV_line.current_lenght;i++)
        {
            cout << "best_candidate_PV_line.depth: " << best_candidate_PV_line.depth << endl;
            Move move = best_candidate_PV_line.moves[i];
            int starting_rank = move.from/8;
            char starting_file = 'a' + (move.from % 8);
            int ending_rank = move.to/8;
            char ending_file = 'a' + (move.to % 8);
            cout << starting_file << starting_rank+1 << " " << ending_file << ending_rank+1 << std::endl;
        }
        cout << "The engine line is: " << endl;
        
        for(int i=0;i<pv_line.current_lenght;i++)
        {
            cout << "pv_line.depth: " << pv_line.depth << endl;
            Move move = pv_line.moves[i];
            int starting_rank = move.from/8;
            char starting_file = 'a' + (move.from % 8);
            int ending_rank = move.to/8;
            char ending_file = 'a' + (move.to % 8);
            cout << starting_file << starting_rank+1 << " " << ending_file << ending_rank+1 << std::endl;
            
        }
        const int displayed_length = min(pv_line.depth,
                          pv_line.current_lenght);
        vector<Move> pv_moves(pv_line.moves,
                      pv_line.moves + displayed_length);
        cout << "Pretty engine line: " << moves_to_PGN(*original, pv_moves) << std::endl;
        cout << "Evaluation after this move: " << pv_line.eval << std::endl;
        copy_BB(wfh+best_move_index,original);
        return pv_line.eval;
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
            auto temp = all_moves(original,p.wfh+200);
            int number_of_new_moves = std::get<0>(temp);
            Move* moves = std::get<1>(temp).data();
            print(original->Board);
            cout << endl << get_FEN(*original) << endl;
            bool WM=original->white_move;
            if(!(p.colour==WM && p.is_human_play)&& p.show_eval)//if its an engine move
            interpret_eval(current_eval);
            
            
            store_original=*original;

            int tt_insertions_before=0, tt_succ_before=0, tt_attempted_before=0;
            if(p.table)
            {
                tt_insertions_before = p.table->number_of_inserions;
                tt_succ_before = p.table->number_of_succ_readouts;
                tt_attempted_before = p.table->number_of_attemted_readouts;
            }

            //time the engine move
            auto start_time = chrono::high_resolution_clock::now();
            if(p.colour==WM && p.is_human_play)
            {
                if(!p.is_pretty_print)
                human_move(original,p.wfh,p.show_eval);
                else
                human_move(original,p.wfh,p.show_eval);
            }
            else if(p.colour!=WM && p.is_human_play)
            {
                current_eval = engine_move(original,p.wfh,p.show_eval,p.depth,p.W,p.table,p.pipe);
            }
            else if(WM)
            {
                current_eval = engine_move(original,p.wfh,p.show_eval,p.depth,p.W_white,p.table,p.pipe);
            }
            else if(!WM)
            {
                current_eval = engine_move(original,p.wfh,p.show_eval,p.depth,p.W_black,p.table,p.pipe);
            }
            auto end_time = chrono::high_resolution_clock::now();
            auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
            
            if(p.is_supposed_to_give_out_move)
            cout << game_lenght  << ". "<< get_move(&store_original,original) << endl;

            if(!p.is_pretty_print&&p.print_Board)
            print(original->Board);
            else if(p.print_Board)
            write_to_python_script(p.pipe,get_UCI(&store_original,original));
            if(!p.is_human_play || p.colour!=WM)
            {
                cout << "Time taken for this move by the engine: " << duration << " ms" << endl;
            }
            //print infor about the lookup table
            if(p.table)
            {
                p.table->get_number_of_entrys();
                int number_of_full_collosion = p.table->get_number_of_full_collisions();
                bool there_are_doubles = p.table->there_are_doubles();
                cout << "Number of full collisions in the lookup table: " << number_of_full_collosion << endl;
                if(there_are_doubles)
                cout << "There are doubles in the lookup table!" << endl;
                else
                cout << "There are no doubles in the lookup table!" << endl;
                p.table->print_readout_delta(tt_insertions_before, tt_succ_before, tt_attempted_before);
                p.table->print_depth_bound_type_histogram();
            }
            
            if(take_history)
            history.push_back(*original);
            game_result=result(original);
            if(game_result)
            {
                print(original->Board);
                cout << endl << get_FEN(*original) << endl;
                break;
            
            }
            
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

double* evaluate_game(vector<BB> history, const int depth, WEIGHTS W=WEIGHTS_OG)
{
    double accuracy_per_move[history.size()-1];
    double score=0;
    double eval_bevore[history.size()-1];
    double eval_after[history.size()-1];
    BB* wfh = new BB[300];
    for(int i=0;i<int(history.size())-1;i++)
    {   
        int corr_f=1-2*!history[i].white_move;
        
        eval_bevore[i]=minimax(&history[i],wfh,depth,W).eval*corr_f;
        auto temp = all_moves(&history[i],wfh);

        int number_of_new_moves = std::get<0>(temp);
        if(depth==0)
        {
            cout << "Error: The game is not over" << endl;
            exit(1);
        }
        
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
        
        
        int depth_to_use=depth;
        if(captures_more_valuable_piece(&history[i],&wfh[index_of_alignment],W))
        depth_to_use++;
        eval_after[i]=minimax(&history[i+1],wfh,depth_to_use,W).eval*corr_f;
        
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
