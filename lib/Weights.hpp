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
        int piece_value[6];//general value of pieces(for materialistic eval); in material_eval() the value with all pieces on (game_phase() 24)
        int piece_value_endgame[6];//with no pieces on (game_phase() 0); material_eval() blends the two
        int offensive_value[6];//attack value of pieces(for king saefty)
        int defensive_value[6];//defence value of pieces(for king safety)
        int king_safety_value;
        int punishment_for_double_pawn;
        int punishment_for_isolated_pawn;
        int punishment_for_trippled_pawn;//also get punishmeht for doubles pawns
        int passed_pawn_value[8];//by relative rank, with no pieces on (#84)
        int passed_pawn_value_opening[8];//with all pieces on; passed_pawn_bonus() blends the two by game_phase()
        // the passer's endgame modifiers (#90), by relative rank where an array, scaled to 0 with all pieces on
        int passed_free_path[8];//no piece on the squares ahead of it
        int passed_king_enemy[8];//per square of the enemy king's distance to its stop square (at most 5)
        int passed_king_own[8];//lost per square of our king's distance to its stop square (at most 5)
        int passed_supported[8];//an own pawn beside it or protecting it
        int passed_rook_behind;//our rook behind it on its file, the same lost for theirs
        int passed_unstoppable;//not scaled: the enemy king cannot catch it by the rule of the square, the enemy having king and pawns only; the side's best passer only
        int value_of_king_safety_for_sorting;//this is a factor!//it should not be changed, untill time is relevant for depth of eval
        // #85: the rest of what basic_eval() uses. A weight set (lib/weight_set.hpp) holds these
        // and the fields above that basic_eval() reads.
        int version;//of the weight set these came from
        int tempo_opening, tempo_endgame;//the side to move's tempo, with all and with no pieces
        // piece_activity_eval(): added for the piece's owner per piece it hits
        int activity_pawn_attack, activity_pawn_defend, activity_pawn_blocked, activity_pawn_push_attack, activity_pawn_push_defend;
        int activity_bishop_defend, activity_bishop_attack;
        int activity_rook_defend, activity_rook_attack;
        int activity_queen_defend, activity_queen_attack;
        int activity_knight_defend, activity_knight_attack;
        int activity_king_defend, activity_king_attack;
        // piece_activity_eval()'s mobility: per piece, by the number of squares it attacks that
        // no own piece stands on, with all pieces on and with none (blended by game_phase())
        int mobility_knight_opening[9], mobility_knight_endgame[9];
        int mobility_bishop_opening[14], mobility_bishop_endgame[14];
        int mobility_rook_opening[15], mobility_rook_endgame[15];
        int mobility_queen_opening[28], mobility_queen_endgame[28];
        // #90's new terms, each added for the owner, with all pieces on and with none (blended by game_phase())
        // pawn_shape_eval():
        int pawn_backward_opening, pawn_backward_endgame;//a pawn with own pawns on a neighbouring file, all of them ahead of it, and an enemy pawn attacking its stop square
        int pawn_phalanx_opening[8], pawn_phalanx_endgame[8];//by relative rank, per two own pawns side by side
        // placement_eval():
        int bishop_pair_opening, bishop_pair_endgame;
        int rook_open_file_opening, rook_open_file_endgame;//no pawn on the rook's file
        int rook_semi_open_file_opening, rook_semi_open_file_endgame;//no own pawn on it, an enemy one
        int rook_seventh_opening, rook_seventh_endgame;//on the relative 7th rank, with enemy pawns on it or the enemy king on the 8th
        int outpost_knight_opening, outpost_knight_endgame;//relative rank 4-6, protected by an own pawn, no enemy pawn left that could attack it
        int outpost_bishop_opening, outpost_bishop_endgame;
        // threat_eval(), not blended: per enemy piece attacked, by its type (pawn rook knight bishop queen)
        int threat_by_pawn[5], threat_by_minor[5], threat_by_rook[5];
        int threat_hanging;//per enemy piece but the king that we attack and they do not defend
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
