// OWNERSHIP=Ascanius
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
constexpr bool DEBUG_MODE = 0;//this activates checks for consistency, like a debug mode


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
constexpr int MAX_PV_Lenght = 128; //maximum principlad variation lenght(cheap)

//==================================================================================================================================
//lookup table sizes (see lib/lookup_table.hpp) - exponent_for_size is log2(number
//of buckets), bucket_size is entries per bucket. TT_entry is ~2.2KB (dominated by
//PV_Line's Move[MAX_PV_Lenght] array), so total size is roughly
//(1<<exponent_for_size) * bucket_size * 2.2KB - keep that in mind before raising
//either exponent.
constexpr int TT_EXPONENT_FOR_SIZE = 15;   //regular per-game transposition table: ~580MB
constexpr int TT_BUCKET_SIZE = 8;
constexpr int PTT_EXPONENT_FOR_SIZE = 16;  //persistent transposition table: ~1.16GB
constexpr int PTT_BUCKET_SIZE = 8;

//==================================================================================================================================
//repetition/cycle detection (see minimax()'s path_history/ply parameters in
//ascaniusfish_2.hpp). Bounds the fixed-size BB path array threaded through the
//search: large enough to cover the 50-move-rule's 100-ply reversible window
//plus generous headroom for deep capture-extended lines (assign_depth can push
//capture sequences well past the nominal search depth). Ply indices at or past
//this bound simply skip repetition checking for that node rather than overflow.
constexpr int MAX_SEARCH_PLY = 512;

//==================================================================================================================================
//null-move pruning (see minimax()'s null-move guard in ascaniusfish_2.hpp).
//R is fixed for now (not adaptive). MIN_DEPTH is kept as its own tunable
//constant, not inlined as "depth-1-NULL_MOVE_REDUCTION>=0", so it can be
//raised independently of R for extra safety margin later.
constexpr bool ENABLE_NULL_MOVE_PRUNING = 1;
constexpr int NULL_MOVE_REDUCTION = 2;
constexpr int NULL_MOVE_MIN_DEPTH = 3;





















#endif // SETTINGS_HPP
