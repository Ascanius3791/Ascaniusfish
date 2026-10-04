// OWNERSHIP=Ascanius
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
        int piece_table_value_opening[6][64];//white's view; black reads them rank-flipped (sq^56)
        int piece_table_value_endgame[6][64];
        int piece_value[6];//general value of pieces(for materialistic eval)
        int offensive_value[6];//attack value of pieces(for king saefty)
        int defensive_value[6];//defence value of pieces(for king safety)
        int king_safety_value;
        int punishment_for_double_pawn;
        int punishment_for_isolated_pawn;
        int punishment_for_trippled_pawn;//also get punishmeht for doubles pawns
        int pawn_supporting_value;
        int passed_pawn_value[8];//by relative rank, full value in the endgame, half with all material on (#84)
        int value_of_king_safety_for_sorting;//this is a factor!//it should not be changed, untill time is relevant for depth of eval
        // #85: the rest of what basic_eval() uses. A weight set (lib/weight_set.hpp) holds these
        // and the fields above that basic_eval() reads.
        int version;//of the weight set these came from
        int mobility_value;//per attacked square
        int tempo_opening, tempo_endgame;//the side to move's tempo, with all and with no material
        // piece_activity_eval(): added for the piece's owner per piece (or square) it hits;
        // "bishop" is every diagonal, queens included, and "rook" every line
        int activity_pawn_attack, activity_pawn_defend, activity_pawn_blocked, activity_pawn_push_attack, activity_pawn_push_defend;
        int activity_bishop_defend, activity_bishop_attack, activity_bishop_square;
        int activity_rook_defend, activity_rook_attack, activity_rook_square;
        int activity_knight_defend, activity_knight_attack, activity_knight_square;
        int activity_king_defend, activity_king_attack;
        // king_safety_eval() (src/king_safety.cpp, #42)
        int ks_attacker_weight[6];
        int ks_min_attackers;
        int ks_hit;
        int ks_weak_ring_square;
        int ks_safe_check[6];
        int ks_unsafe_check;
        int ks_no_queen;
        int ks_danger_div;
        int ks_shelter[8];
        int ks_open_file;
        int ks_storm[8];
        int ks_storm_blocked_div;
        int ks_full_piece_material;
            void change_values(int alpha, bool change_white_pawn_values, int probabiltiy_of_change =100);

            void print_values_to_file(std::string filename)const;
            
            void read_values_from_file(std::string filename);

            void print_all_values()const;

            int norm_to(const WEIGHTS& W) const;//returns the (sq)norm of the difference of the weights

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
