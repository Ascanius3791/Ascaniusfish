#ifndef BITBOARD_INITIALISATIONS_HPP
#define BITBOARD_INITIALISATIONS_HPP
#include"../src/Bitboards.cpp"
#include<climits>

class initialize_FEN_to
{
    public:
    static void translate( const bool original[8][8][2][6] , uint64_t goal[12]);

    static void translate(const uint64_t original[12], bool goal[8][8][2][6]);

    static void empty(bool FEN[8][8][2][6] );

    static void empty(uint64_t Board[12]);
    
    static void Standartboard(uint64_t Board[12]);

    static void Standartboard(bool FEN[8][8][2][6] );

    static void Position_1(uint64_t Board[12]);

    static void puzzle_1(bool FEN[8][8][2][6] );

    static void puzzle_1(uint64_t Board[12] );

    static void puzzle_2(bool FEN[8][8][2][6] );

    static void puzzle_2(uint64_t Board[12] );

    static void puzzle_3(bool FEN[8][8][2][6] );

    static void puzzle_3(uint64_t Board[12] );

    static void puzzle_4(bool FEN[8][8][2][6] );

    static void puzzle_4(uint64_t Board[12] );

    static void puzzle_5(bool FEN[8][8][2][6]);

    static void puzzle_5(uint64_t Board[12] );

    static void position_2(uint64_t Board[12] );

    static void random_position( uint64_t Board[12], int max_eval_diff=INT_MAX, int num_of_pieces=32, bool pawn=1, bool rook=1, bool knight=1, bool bishop=1, bool queen=1);

    static void queen_mate(uint64_t Board[12]);
    
    static void rook_mate(uint64_t Board[12]);
    
    static void kn_bishop_mate(uint64_t Board[12]);
    
    static void bishop_pair_mate(uint64_t Board[12]);
    
    static void rook_vs_queen_mate(uint64_t Board[12]);

    static void ruy_lopez_berlin_defense(uint64_t Board[12]);

    static void kings_gambit(uint64_t Board[12]);

    static void four_knights_scotch(uint64_t Board[12]);

    static void caro_can(uint64_t Board[12]);
};



#endif//BITBOARD_INITIALISATIONS_HPP