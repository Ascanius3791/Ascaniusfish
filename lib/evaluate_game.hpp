// OWNERSHIP=Ascanius
#ifndef EVALUATE_GAME_HPP
#define EVALUATE_GAME_HPP
#include<algorithm>
#include<cmath>

double round_to_percentage(double number)
{
    double return_value= std::round(number*100)/100;
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
        vector<int> assigned_depth = assign_depth(&history[i],wfh,number_of_new_moves,remaining_calls,W);
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
        
        
        int child_remaining_calls=resolved_assigned_depth(assigned_depth[index_of_alignment],remaining_calls);
        eval_after[i]=minimax(&history[i+1],wfh,child_remaining_calls,0,W)*corr_f;
        
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
