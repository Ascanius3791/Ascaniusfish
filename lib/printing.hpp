#ifndef PRINTING_HPP
#define PRINTING_HPP

#include<iostream>
#include<vector>
#include<string>
#include "../src/Bitboards.cpp"
#include "../src/Settings.cpp"

void print(int piece, int piece_table_value_opening[7][64], float weight_on_opening=0, int piece_table_value_endgame[7][64]=NULL);//last piece is the black pawn

bool print(const uint64_t Board[12], bool comment=0, std::string my_comment = "No Comment");//returns 0 if not ecxaption has been found 1 else

bool print(const uint64_t Board, bool comment=0, std::string my_comment = "No Comment");//returns 0 if not ecxaption has been found 1 else

void print(const bool FEN[8][8][2][6]);

void print(const std::vector<BB>all_boards, int N=1);

void print(const BB* const Base, int start, int end);




















#endif // PRINTING_HPP