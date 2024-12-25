#ifndef SETTINGS_HPP
#define SETTINGS_HPP


//modedefine;
//==================================================================================================================================

constexpr bool pretty_mode                =1;
constexpr bool time_usage_display         =1;
constexpr bool in_check_time_display      =1;
constexpr bool all_moves_time_display     =1;
constexpr bool lookuptable_time_display   =0;
constexpr bool eval_time_display          =1;
constexpr bool checkmate_stalemate_time_display =1;
constexpr bool surpress_print_globally    =0;

constexpr bool extensive_time_display = 0;
constexpr bool take_history = 1;
constexpr bool take_precautions = 1;//this activates checks for consistency, like a debug mode


//==================================================================================================================================
constexpr bool all_move_type = 1;
//moves
constexpr bool pawn_push=1;
constexpr bool pawn_capture=1;
constexpr bool rook=1;
constexpr bool knight=1;
constexpr bool bishop=1;
constexpr bool king=1;

//==================================================================================================================================
//special settings
constexpr int max_mating_seq = 1000;





















#endif // SETTINGS_HPP