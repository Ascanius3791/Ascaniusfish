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
//of buckets), bucket_size is entries per bucket. TT_entry is ~184 bytes (PV_Line
//keeps PV_CHUNK moves inline instead of a flat Move[MAX_PV_Lenght]), so total size
//is roughly (1<<exponent_for_size) * bucket_size * 184B.
//
//DO NOT RAISE THE EXPONENT just because there is memory to spare. The table is
//deliberately small: a small footprint is what lets several engine processes
//run at the same time - debugging a binary next to a probe/benchmark, and two
//agents working in this repo who may each want to play a game. That parallelism is
//worth more here than the marginal search gain from a bigger table, so raising
//it is Ascanius' call and not a free win. Going SMALLER is fine and already
//supported: -DTT_EXPONENT=n (tools/match builds its engines with tt=14).
#ifndef TT_EXPONENT
#define TT_EXPONENT 15
#endif
constexpr int TT_EXPONENT_FOR_SIZE = TT_EXPONENT;   //regular per-game transposition table: 262144 entries, ~46MB (-DTT_EXPONENT=n overrides it downwards, e.g. for parallel match engines)
constexpr int TT_BUCKET_SIZE = 8;

//true (default): a stored bound only cuts, it never narrows alpha and beta (#64).
//-DTT_BOUNDS_NEVER_NARROW=0 builds the variant whose UCI options TTNarrowing and
//TTNarrowingDeeper switch the pre-#64 narrowing back on at runtime (#65).
#ifndef TT_BOUNDS_NEVER_NARROW
#define TT_BOUNDS_NEVER_NARROW 1
#endif
constexpr bool tt_bounds_never_narrow = TT_BOUNDS_NEVER_NARROW;

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

//==================================================================================================================================
//principal variation search and late move reductions (see minimax()'s move loop in
//ascaniusfish_2.hpp). PVS: every move after the first is searched with a null
//window first and re-searched with the full one only if it lands inside it.
//LMR: quiet moves ordered after the first LMR_FULL_DEPTH_MOVES, at depth >=
//LMR_MIN_DEPTH, are searched lmr_reduction(depth, move number) plies shallower -
//never the TT move, a killer, a check or a move out of check - and re-searched at
//full depth if they beat the bound. The reduction is
//LMR_BASE + ln(depth)*ln(move number)/LMR_DIVISOR, rounded down, at least 1: the deeper the
//node and the later the move, the less it is trusted to matter.
constexpr bool ENABLE_PVS = 1;
constexpr bool ENABLE_LMR = 1;
constexpr int LMR_MIN_DEPTH = 3;
constexpr int LMR_FULL_DEPTH_MOVES = 3;
constexpr double LMR_BASE = 0.75;
constexpr double LMR_DIVISOR = 2.25;





















#endif // SETTINGS_HPP
