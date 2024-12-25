#ifndef BITBOARDS_HPP
#define BITBOARDS_HPP

#include <iostream>
#include <string>
#include "../src/bit_operations.cpp"

struct BB
{
    uint64_t Board[12];
    bool white_move;
    bool castle[2][2]; //first index is color, second index is side 0=black 1=white, 0=queen side, 1=king side
    uint64_t en_passant;
    mutable int number_of_repetitions;
    mutable int is_evaluated;
    int move;
    int halfmoves_since_last_capture_or_pawn_move;
    mutable int eval;//for the lookup table
    mutable int depth_of_eval;
   //mutable BB* parent=NULL;
    //int alpha=INT_MIN, beta=INT_MAX;

    //constructors
    BB();
    BB(const std::string FEN);
    BB(const BB* const original,std::string mode = "copy");

    //functions

    std::string get_UCI(const BB* const goal);

    std::string get_FEN();


    void check_castling_rights();

    bool operator==(const BB* const rhs) const;

    bool operator==(const BB& rhs) const;

};

void castling_right_rook_correction_for_col(BB* const original, int for_white);// corrects for presence of rooks

void castling_rights(BB* const original);

bool are_equal(const BB* const  BB_1,const BB* const BB_2);

std::string get_coordinate_PGN(std::vector<BB> history);
    































#endif // BITBOARDS_HPP