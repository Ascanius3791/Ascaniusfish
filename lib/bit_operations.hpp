#ifndef BIT_OPERATIONS_HPP
#define BIT_OPERATIONS_HPP

#include<iostream>




int count(uint64_t n);//counts the number of bits set in n


inline void clear_sq(uint64_t Board[12],int square);//clears the square of all pieces

inline void place_piece(uint64_t Board[12],int piece,int square);//places a piece on the square

inline void clear_sq_of_enemy(uint64_t to_be_cleard[12] , int square , bool white_move);// clears square only of enemy!


inline int find_and_delete_trailling_1(uint64_t &n);//finds the rightmost 1 in n and deletes it


















#endif // BIT_OPERATIONS_HPP