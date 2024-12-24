#ifndef WEIGHTS_HPP
#define WEIGHTS_HPP
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>


class WEIGHTS
{
    public:
        int skip_depth_decrease_threshold;
        int value_of_attacked_square;//number of moves avaliable
        int check_value;
        int piece_table_value_opening[7][64];//7 is the black pawn
        int piece_table_value_endgame[7][64];//7 is the black pawn
        int piece_value[6];//general value of pieces(for materialistic eval)
        int offensive_value[6];//attack value of pieces(for king saefty)
        int defensive_value[6];//defence value of pieces(for king safety)
        int king_safety_value;
        int punishment_for_double_pawn;
        int punishment_for_isolated_pawn;
        int punishment_for_trippled_pawn;//also get punishmeht for doubles pawns
        int pawn_supporting_value;
        int value_of_king_safety_for_sorting;//this is a factor!//it should not be changed, untill time is relevant for depth of eval
            void change_values(int alpha, bool change_white_pawn_values, int probabiltiy_of_change =100);

            void print_values_to_file(std::string filename)const;
            
            void read_values_from_file(std::string filename);

            void print_all_values()const;

            int norm_to(WEIGHTS W);//returns the (sq)norm of the difference of the weights

            void append_weights_to_File(const std::string& filename = "History_of_Weights.txt")const;

            private:

            bool is_correct_password()const;

            public:

            void clearFileExceptFirstLine(const std::string& filename = "History_of_Weights.txt")const;

            WEIGHTS();
               
};

const WEIGHTS WEIGHTS_OG;

void reset_weights_txt_and_History_of_Weight();
















#endif // WEIGHTS_HPP