#ifndef BIT_OPERATIONS_CPP
#define BIT_OPERATIONS_CPP

#include "../lib/bit_operations.hpp"

int count(uint64_t n) 
    {
        int count = 0;
        while (n) {
            count += n & 1;
            n >>= 1;
        }
        return count;
    }

inline void clear_sq(uint64_t Board[12],int square)
{
    for(int piece=0;piece<12;piece++)
    {
        Board[piece] &= ~(1ULL << square);
    }
    
}

inline void place_piece(uint64_t Board[12],int piece,int square)
{
    clear_sq(Board,square);  
    Board[piece] |= (1ULL << square);
    
}

inline void clear_sq_of_enemy(uint64_t to_be_cleard[12] , int square , bool white_move)// clears square only of enemy!
{
    for(int i=0;i<6;i++)
    to_be_cleard[i+6*white_move] &= ~(1Ull << square);
}

inline int find_and_delete_trailling_1(uint64_t &n)
{
    int i= __builtin_ctzll(n);
    n = n & (n-1);
    return i;
    
}




















#endif // BIT_OPERATIONS_CPP