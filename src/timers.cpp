#ifndef TIMERS_CPP
#define TIMERS_CPP

#include "../lib/timers.hpp"


//TM
void TM::start_time()
{
    start = std::chrono::high_resolution_clock::now();
    num_of_starts++;
}

void TM::end_time()
{
    end = std::chrono::high_resolution_clock::now();
    duration = duration + (end-start);
    num_of_ends++;
}

void TM::display_time()
{
    std::cout << duration.count() << "s" << std::endl;
    if(num_of_starts!=num_of_ends)
    std::cout << "(num_of_starts, num_of_ends)(dont align): (" << num_of_starts<< " " << num_of_ends << ")" << std::endl;
    
}

void TM::display_average_time()
{   
    if(num_of_starts!=num_of_ends)
    std::cout << "(num_of_starts, num_of_ends)(dont align): (" << num_of_starts<< " " << num_of_ends << ")" << std::endl;
    std::cout << double(duration.count())/num_of_starts << "s" << " num: " << num_of_starts <<  std::endl;
}

void TM::display_average_time_in_units_of(int unit)
{
    if(num_of_starts!=num_of_ends)
    std::cout << "(num_of_starts, num_of_ends)(dont align): (" << num_of_starts<< " " << num_of_ends << ")" << std::endl;
    std::cout << double(duration.count())/num_of_starts/unit << "s" << std::endl;
}

void TM::NULLIFY()
{
    duration=std::chrono::duration<double>::zero();
    num_of_starts=0;
    num_of_ends=0;
}

TM::TM()
{
    NULLIFY();
}




//stack_timer
int stack_timer::completed_timers = 0;
std::chrono::duration<double> stack_timer::total_duration = std::chrono::duration<double>::zero();

stack_timer::stack_timer()
{
    start = std::chrono::high_resolution_clock::now();
}

stack_timer::~stack_timer()
{
    end = std::chrono::high_resolution_clock::now();
    total_duration = total_duration + (end-start);
    completed_timers++;
}

void stack_timer::reset()
{
    total_duration = std::chrono::duration<double>::zero();
    completed_timers = 0;
}


//timers

TM in_check_time;
TM all_moves_time;
TM KING_CHECK;
TM ROOK_CHECK;
TM KNIGHT_CHECK;
TM BISHOP_CHECK;
TM PAWN_CHECK;


TM AM_INTRO;// AM = all moves
TM AM_PAWN_PUSH;
TM AM_PAWN_CAPTURE;
TM AM_ROOK_R;
TM AM_ROOK_Q;
TM AM_KNIGHT;
TM AM_BISHOP_B;
TM AM_BISHOP_Q;
TM AM_KING;
TM AM_CASTLE_K;
TM AM_CASTLE_Q;
TM AM_OUTRO;



#endif //TIMERS_CPP
