// OWNERSHIP=Ascanius
#ifndef EVAL_HPP
#define EVAL_HPP
#include "../src/basic_eval.cpp"
#include "../src/move_generation.cpp"
#include "../src/Weights.cpp"
#include <limits.h>

int exception_eval(const BB* const original);

int eval(const BB* const original, WEIGHTS W =WEIGHTS_OG, int exception_state=-1);










#endif
