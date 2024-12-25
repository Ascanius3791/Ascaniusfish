#ifndef MOVE_GENERATION_HPP
#define MOVE_GENERATION_HPP
#include "../src/checks.cpp"

int all_moves(const BB* const original, BB* const wfh , int len_wfh=INT_MAX);// returns number of moves and writes them to wfh(write from here)

bool one_move(const BB* const original);//returns true if there are legal moves




#endif // MOVE_GENERATION_HPP