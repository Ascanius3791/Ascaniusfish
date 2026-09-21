// OWNERSHIP=Ascanius
#ifndef EVAL_CPP
#define EVAL_CPP
#include "../lib/eval.hpp"

int exception_eval(const BB* const original)//0=no exception, 1=stalemate, 2=checkmate
{
    
    if(one_move(original))
    return 0;
    if(!in_check(original->Board,original->white_move))
    {
        return 1;
    }
    else
    {
        return 2;
    }
}

int eval(const BB* const original, WEIGHTS W, int exception_state)//exception state may be explicitly given, if it is not, it will be calculated. 0=no exception, 1=stalemate, 2=checkmate
{
    if(exception_state==-1)
    exception_state = exception_eval(original);//in case of exceptions, this handels the assignment of the eval to original
    if(exception_state==0)
    {
        int eval=basic_eval(original,W);
        return eval;
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







#endif
