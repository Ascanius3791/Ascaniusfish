#ifndef CHECKS_HPP
#define CHECKS_HPP

#include"magics.hpp"
#include"../src/Bitboards.cpp"
#include"../src/printing.cpp"
#include"../src/templates.cpp"
#include"../src/timers.cpp"
#include<string>

bool missing_king(uint64_t Board[12], std::string message ="");

bool pawn_check_0(uint64_t Board[12] ,bool white_move );

bool pawn_check_1(uint64_t Board[12] ,bool white_move);

bool knight_check_0(uint64_t Board[12] ,bool white_move );

bool knight_check_1(uint64_t Board[12] ,bool white_move );

uint64_t pawn_attacks(uint64_t Board[12], bool white_move);

bool in_check(const uint64_t Board[12] ,bool white_move);




#endif // CHECKS_HPP