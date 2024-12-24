#ifndef TIMERS_HPP
#define TIMERS_HPP

#include<iostream>
#include<chrono>

class TM//time measurement
{
    public:
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::chrono::time_point<std::chrono::high_resolution_clock> end;
    std::chrono::duration<double> duration;
    int num_of_starts, num_of_ends;//these timers are not intended to end, at teh end of a scope, hence must keep track of num running timers

    void start_time();

    void end_time();

    void display_time();

    void display_average_time();

    void display_average_time_in_units_of(int unit);

    void NULLIFY();

    TM();
};

class stack_timer
{
    private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::chrono::time_point<std::chrono::high_resolution_clock> end;

    public:
    static int completed_timers;

    static std::chrono::duration<double> total_duration;

    static void reset();


    stack_timer();

    ~stack_timer();

};
















#endif // TIMERS_HPP