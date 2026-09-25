// OWNERSHIP=Ascanius
#include"ascaniusfish.hpp"
#include "lib/cuckoo_cycle_table.hpp"
#include "lib/search_control.hpp"
#include <algorithm>
#include <cstdlib>//for communication with python
#include <thread>
#include <unordered_map>
#include <array>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef ascaniusfish_2
#define ascaniusfish_2

using namespace std;
int prunable_moves_total=0;
int pruned_moves=0;
//==================================================================================================================================
constexpr int max_number_of_threads=4;
int number_of_threads=1;
int number_of_mimimax_calls=0;
int number_of_half_moves=0;


class tournament
{
    public:
    string name = "Tournament";
    int white_wins=0;
    int black_wins=0;
    int draws=0;
    int terminated_games=0;
    void reset()
    {
        white_wins=0;
        black_wins=0;
        draws=0;
        terminated_games=0;
    }
};

tournament my_tournament;

class pruning
{
    public:
    int alpha=INT_MIN;
    int beta=INT_MAX;
    int eval=0;
    vector<int> indices;
    int move_number=0;
    int max_move_number=0;
};
//==================================================================================================================================

void initialize_rand()
{
    unsigned int my_int=0;
    unsigned int* ptr=&my_int;   
    unsigned long long int address = reinterpret_cast<unsigned long long int>(ptr);
    srand(address);
}

vector<int> sorting_moves(const BB* const Base, vector<Move> moves, int num, bool WM, const PV_Line* const pv_line =0, int index_of_pv_line_to_compare_against=0, WEIGHTS W = WEIGHTS_OG)//returns 0, till end-start-1 ,ordered!
{
    // Create an array of indices [start, end)
    vector<int> indices(num);
    vector<int> sorting_scores(num);
    for (int i = 0; i < num; ++i) {
        indices[i] = i;
        sorting_scores[i] = sorting_eval(Base + i, W);
    }

    sort(indices.begin(), indices.end(), [&sorting_scores, WM](int a, int b)
    {
        return WM
            ? sorting_scores[a] > sorting_scores[b]
            : sorting_scores[a] < sorting_scores[b];
    });
    if (pv_line && pv_line->current_lenght !=0)
    {
        Move pv_move = pv_line->moves[index_of_pv_line_to_compare_against];

        for (int i = 0; i < num; ++i)
        {
            if (moves[indices[i]] == pv_move)
            {
                rotate(indices.begin(), indices.begin() + i, indices.begin() + i + 1);
                break;
            }
        }
    }
    //if pv_line is provided set that move on top
    return indices;
}

// Builds the PV_Line returned when the repetition/cycle heuristic below
// fires. For an interior node (ply>root_ply) the bare, move-less version is
// all that's needed - the parent wraps it with ITS OWN move via
// PV_Line(move,depth,&candidate), so candidate.moves[] is never read. But
// when this fires exactly at the root of a search tree (ply==root_ply),
// there's no parent to attach a move to, and engine_move() needs
// pv_line.moves[0] to know what to actually play. Leaving it as an unmatched
// default Move there used to make engine_move()'s move-match loop silently
// fall through to best_move_index's 0-initialized default - i.e. "play
// whatever all_moves() happened to list first" - any time the actual current
// game position had already recurred once (a real, reachable case: any
// slow/shuffling position can trip it). The eval/bound_type/draw semantics
// are unchanged either way - this only ever adds move info, never changes
// the score.
PV_Line make_repetition_draw_pv_line(const BB* const original, BB* const wfh, int depth, int ply, int root_ply, WEIGHTS W)
{
    PV_Line draw_pv_line = PV_Line(0);
    draw_pv_line.depth = depth;
    draw_pv_line.current_lenght = 0;
    draw_pv_line.bound_type = 0;

    if(ply==root_ply)
    {
        auto result = all_moves(original, wfh);
        int number_of_new_moves = std::get<0>(result);
        vector<Move> moves = std::get<1>(result);
        if(number_of_new_moves>0)
        {
            vector<int> indices = sorting_moves(wfh, moves, number_of_new_moves, original->white_move, nullptr, 0, W);
            draw_pv_line.moves[0] = moves[indices[0]];
            draw_pv_line.current_lenght = 1;
        }
    }
    return draw_pv_line;
}

// Hard upper bound on how deep minimax_tactical()'s recursion can ever go (see
// minimax_tactical() below): it recurses only into moves for which
// is_good_capture() holds, and every such move removes exactly one non-king piece
// from the board (promotion alone doesn't change the count). At most 15 non-king
// pieces per side -> at most 30 ever -> the recursion can't exceed 30 plies. This
// is a proven combinatorial bound, not a heuristic, so it needs no safety margin.
constexpr int max_non_king_pieces = 30;

// Dedicated tactical/quiescence-style leaf search, invoked from minimax() when
// depth==0 (replacing the old direct eval() leaf). All legal moves are generated;
// only "tactical" moves (for now: good captures per is_good_capture()) are explored
// further, everything else is discarded. No lookup-table probe/insert during the
// recursion itself (not worth it at this granularity) - the lookup table is only
// consulted, read-only, at a genuinely quiet leaf (no tactical moves available),
// and only an exact (bound_type==0) entry is trusted; otherwise the board is
// evaluated directly. See max_non_king_pieces above for why this recursion is
// hard-bounded without needing its own depth/ply counter.
PV_Line minimax_tactical(const BB* const original, BB* const wfh, WEIGHTS W = WEIGHTS_OG, int alpha = INT_MIN, int beta = INT_MAX, lookup_table* const table = NULL)
{
    poll_search_abort();//see minimax()
    auto result = all_moves(original, wfh);
    int number_of_new_moves = std::get<0>(result);
    vector<Move> moves = std::get<1>(result);
    vector<int> indices = sorting_moves(wfh, moves, number_of_new_moves, original->white_move, nullptr, 0, W);

    vector<int> tactical_order;
    for(int idx : indices)
        if(
            moves[idx].promotion_piece_type!=-1 ||
            is_good_capture(original, moves[idx].from, moves[idx].to, moves[idx].is_en_passant, W)
            )
        tactical_order.push_back(idx);

    if(tactical_order.empty())
    {
        if(number_of_new_moves==0)
        {
            int best_eval = in_check(original->Board, original->white_move) ? (original->white_move ? INT_MIN : INT_MAX) : 0;
            PV_Line exception_pv_line = PV_Line(best_eval);
            exception_pv_line.current_lenght = 0;
            exception_pv_line.bound_type = 0;
            return exception_pv_line;
        }
        if(table)
        {
            TT_readout readout = table->is_retrivable_eval(original, 0);
            if(readout.is_found && readout.pv_line.bound_type==0)
            return readout.pv_line;
        }
        int evaluation = eval(original, W, 0); // exception_state=0: number_of_new_moves>0 above already proves the game isn't over
        PV_Line leaf = PV_Line(evaluation);
        leaf.current_lenght = 0;
        leaf.bound_type = 0;
        return leaf;
    }

    PV_Line pv_line = PV_Line(original->white_move ? INT_MIN : INT_MAX);
    for(int idx : tactical_order)
    {
        PV_Line candidate = minimax_tactical(wfh+idx, wfh+number_of_new_moves, W, alpha, beta, table);
        int child_eval = candidate.eval;
        if(child_eval<INT_MIN+max_mating_seq)
        child_eval++;
        if(child_eval>INT_MAX-max_mating_seq)
        child_eval--;
        candidate.eval = child_eval;
        bool improves_pv;
        if(original->white_move)
        {
            improves_pv = child_eval>pv_line.eval;
            pv_line.eval=max(pv_line.eval,child_eval);
            alpha=max(alpha,child_eval);
        }
        else
        {
            improves_pv = child_eval<pv_line.eval;
            pv_line.eval=min(pv_line.eval,child_eval);
            beta=min(beta,child_eval);
        }
        if(improves_pv)
        pv_line = PV_Line(moves[idx], 0, &candidate);
        if(beta<=alpha)
        break;
    }
    return pv_line;
}

PV_Line minimax(const BB*const original ,BB* const wfh ,int depth = 0, WEIGHTS W= WEIGHTS_OG,int alpha = INT_MIN, int beta = INT_MAX,lookup_table* const table=NULL, BB* const path_history=nullptr, int ply=0, const CuckooCycleTable* const cycle_table=nullptr, int root_ply=INT_MIN, bool null_move_allowed=true)
{
    if(DEBUG_MODE)
    saefty_checks(original);
    number_of_mimimax_calls++;
    poll_search_abort();//UCI "stop"/movetime: throws search_aborted, see lib/search_control.hpp

    // Marks where THIS search tree started - see make_repetition_draw_pv_line()
    // above. Sentinel default means every existing external caller is
    // automatically its own root; no call site other than the recursive one
    // below needs to change.
    int effective_root_ply = (root_ply==INT_MIN) ? ply : root_ply;

    // Repetition/cycle bookkeeping - just records this node's board for
    // descendants to check against. This USED to also short-circuit the whole
    // node to an immediate draw as soon as any repeat/cycle was detected here,
    // before any move was even generated - which scored the position as a draw
    // unconditionally, regardless of whether the side to move actually had a
    // better alternative (a completely winning position with a repetition
    // available anywhere in reach would be scored as 0, even though the side
    // to move would obviously never choose to repeat). The correct place to
    // apply the draw score is per-CANDIDATE-MOVE in the loop below - compared
    // against every other candidate via the normal alpha-beta max/min - not as
    // a blanket override for the entire node. See the per-move checks below.
    if(path_history && ply<MAX_SEARCH_PLY)
    {
        path_history[ply] = *original;
    }

    // One reversible move from recreating an earlier position - opposite side
    // to move right now, so only odd ply gaps are candidates. Starts at
    // ply-3, NOT ply-1: ply_gap=1 means "the immediate parent", and undoing
    // whatever move was just played to reach `original` is ALWAYS available
    // for any quiet move - that's not a cycle, it's just what "reversible"
    // means. Computed once here (it only depends on `original`, not on which
    // candidate move we're about to consider) and used per-move in the loop
    // below, since `found_move` is exactly one specific move `original` could
    // play - only THAT move's evaluation should be replaced with a draw score.
    Move cycle_avoiding_move;
    bool cycle_move_found = false;
    if(path_history && cycle_table && ply<MAX_SEARCH_PLY)
    {
        int window_start = max(0, ply - original->halfmoves_since_last_capture_or_pawn_move);
        for(int i=ply-3; i>=window_start; i-=2)
        {
            int found_piece_type;
            Move found_move;
            if(cycle_table->detect_upcoming_cycle(*original, path_history[i], ply-i, found_piece_type, found_move))
            {
                cycle_avoiding_move = found_move;
                cycle_move_found = true;
                break;
            }
        }
    }

    PV_Line tt_hint;
    bool is_tt_hint_found=0;
    if(table)
        {
            TT_readout readout = table->is_retrivable_eval(original, depth);

            if(readout.is_found)
            {
                bool is_proven_mate = readout.pv_line.bound_type==0
                    && (readout.pv_line.eval <= INT_MIN + max_mating_seq
                        || readout.pv_line.eval >= INT_MAX - max_mating_seq);
                if((depth<=readout.pv_line.depth || is_proven_mate) && readout.pv_line.current_lenght>0)
                {
                    if(readout.pv_line.bound_type==0)//exact
                    return readout.pv_line;
                    else if(depth==readout.pv_line.depth)//only on exact depth alpha and beta can be updated
                    {
                        //update alpha and beta based on the bound type
                        if(readout.pv_line.bound_type == -1) // lower bound
                        alpha = max(alpha, readout.pv_line.eval);
                        else if(readout.pv_line.bound_type == 1) // upper bound
                        beta = min(beta, readout.pv_line.eval);
                        if(alpha >= beta)
                        {
                            // Prune the search
                            return readout.pv_line;
                        }
                    }
                }
                tt_hint=readout.pv_line;
                is_tt_hint_found=1;
            }
        }
        
    if(depth==0)
    {
        return minimax_tactical(original, wfh, W, alpha, beta, table);
    }
    
    int exception_state=exception_eval(original);
    if(exception_state==1||exception_state==2)
        {
            int best_eval;
            if(exception_state==1)
            best_eval=0;
            else if(exception_state==2)
            {
                best_eval=original->white_move ? INT_MIN : INT_MAX;
            }
            PV_Line exception_pv_line = PV_Line(best_eval);
            exception_pv_line.depth=depth;
            exception_pv_line.current_lenght=0;
            exception_pv_line.bound_type = 0; // exact evaluation
            if(table)
            {
                TT_entry entry;
                entry.zobrist_hash=original->zobrist_hash;
                entry.board = *original;
                entry.initialized = true;
                entry.pv_line = exception_pv_line;
                entry.pv_line.bound_type = 0;
                table->insert(entry);
            }
            return exception_pv_line;
        }

    // Null-move pruning: assume the side to move could do nothing at all and
    // still search a reduced-depth response - if that's already enough to
    // beat the current bound, a real move will almost certainly do at least
    // as well, so we cut off without generating/searching the real move list.
    // ply!=effective_root_ply (not ply>0) mirrors make_repetition_draw_pv_line's
    // root check above: engine_move() can seed ply at a nonzero value from
    // real game history, so a literal ply>0 test would fire at the wrong node.
    // The zugzwang guard (side_to_move_lacks_non_pawn_material) is a crude
    // first cut - see its TODO in src/basic_eval.cpp.
    if(ENABLE_NULL_MOVE_PRUNING
       && depth >= NULL_MOVE_MIN_DEPTH
       && ply != effective_root_ply
       && null_move_allowed
       && !side_to_move_lacks_non_pawn_material(original)
       && !in_check(original->Board, original->white_move))
    {
        BB null_child(original, "base"); // flips side to move, clears en passant, keeps castling rights
        Zobrist::update_zobrist_hash_null_move(*original, null_child);
        int null_depth = depth - 1 - NULL_MOVE_REDUCTION;

        if(original->white_move)
        {
            // Max node: can the opponent, even with a free tempo, still be held to eval>=beta?
            PV_Line null_pv = minimax(&null_child, wfh, null_depth, W, beta-1, beta, table,
                                       nullptr, ply+1, nullptr, effective_root_ply, false);
            int null_eval = null_pv.eval;
            if(null_eval<INT_MIN+max_mating_seq)//same mate-distance fixup as the real move loop below
            null_eval++;
            if(null_eval>INT_MAX-max_mating_seq)
            null_eval--;
            if(null_eval >= beta)
            {
                PV_Line cutoff_pv = PV_Line(null_eval);
                cutoff_pv.depth = depth;
                cutoff_pv.current_lenght = 0;
                cutoff_pv.bound_type = -1; // lower bound / fail-high
                if(table)
                {
                    TT_entry entry;
                    entry.board = *original;
                    entry.zobrist_hash = original->zobrist_hash;
                    entry.pv_line = cutoff_pv;
                    entry.initialized = true;
                    table->insert(entry);
                }
                return cutoff_pv;
            }
        }
        else
        {
            // Min node: can white, even with a free tempo, still be held to eval<=alpha?
            PV_Line null_pv = minimax(&null_child, wfh, null_depth, W, alpha, alpha+1, table,
                                       nullptr, ply+1, nullptr, effective_root_ply, false);
            int null_eval = null_pv.eval;
            if(null_eval<INT_MIN+max_mating_seq)
            null_eval++;
            if(null_eval>INT_MAX-max_mating_seq)
            null_eval--;
            if(null_eval <= alpha)
            {
                PV_Line cutoff_pv = PV_Line(null_eval);
                cutoff_pv.depth = depth;
                cutoff_pv.current_lenght = 0;
                cutoff_pv.bound_type = 1; // upper bound / fail-low
                if(table)
                {
                    TT_entry entry;
                    entry.board = *original;
                    entry.zobrist_hash = original->zobrist_hash;
                    entry.pv_line = cutoff_pv;
                    entry.initialized = true;
                    table->insert(entry);
                }
                return cutoff_pv;
            }
        }
    }

    auto result = all_moves(original,wfh);
    int number_of_new_moves = std::get<0>(result);
    vector<Move> moves = std::get<1>(result);
    vector<int> indices = sorting_moves(wfh,moves,number_of_new_moves,original->white_move,&tt_hint,0,W);// why on earth would this be slower? its pruning ration is better, by a lot!
    prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
    Move best_move;
    const int alpha_0 = alpha, beta_0 = beta;
    PV_Line pv_line =PV_Line(original->white_move ? INT_MIN : INT_MAX);//initialize with worst possible value for the player to move
    pv_line.depth=depth;
    for(int i=0;i<number_of_new_moves;i++)
    {    
        int depth_to_use=depth-1;
        Move move =moves[indices[i]];
        BB* child = wfh+indices[i];

        // Would THIS specific move recreate an earlier position (exact repeat),
        // or is it the specific move detect_upcoming_cycle identified as leading
        // toward one (see above)? If so, its value is a draw - fed into the
        // normal comparison below exactly like any other candidate's eval, so a
        // better alternative move still wins if one exists.
        bool forced_draw = false;
        if(path_history && ply+1<MAX_SEARCH_PLY)
        {
            int child_window_start = max(0, (ply+1) - child->halfmoves_since_last_capture_or_pawn_move);
            for(int j=ply-1; j>=child_window_start; j-=2) // (ply+1)-2, nearest same-side-to-move ancestor
            {
                if(are_equal(&path_history[j], child)) { forced_draw = true; break; }
            }
            if(!forced_draw && cycle_move_found && move==cycle_avoiding_move)
            forced_draw = true;
        }

        PV_Line candidate_pv_line = forced_draw
            ? make_repetition_draw_pv_line(child, wfh+number_of_new_moves, depth_to_use, ply+1, effective_root_ply, W)
            : minimax(child,wfh+number_of_new_moves,depth_to_use,W,alpha,beta,table,path_history,ply+1,cycle_table,effective_root_ply);
        int eval = candidate_pv_line.eval;
        if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
        eval++;
        if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
        eval--;
        candidate_pv_line.eval=eval;
        bool improves_pv;
        if(original->white_move)
        {
            improves_pv = eval>pv_line.eval;
            pv_line.eval=max(pv_line.eval,eval);
            alpha=max(alpha,eval);
        }
        else
        {
            improves_pv = eval<pv_line.eval;
            pv_line.eval=min(pv_line.eval,eval);
            beta=min(beta,eval);
        }
        if(improves_pv)
        {
            pv_line = PV_Line(move,depth,&candidate_pv_line);
        }
        
        if(beta<=alpha)
        {
            pruned_moves+=number_of_new_moves-i-1;
            break;
        }        
    }
    //now correct for fail low
        if(pv_line.eval <= alpha_0)
            pv_line.bound_type = 1;   // upper bound (fail-low)
        else if(pv_line.eval >= beta_0)
            pv_line.bound_type = -1;  // lower bound (fail-high)
        else
            pv_line.bound_type = 0;   // exact
    if(table)
    {
        TT_entry entry;
        entry.board = *original;
        entry.zobrist_hash=original->zobrist_hash;
        entry.pv_line = pv_line;
        entry.initialized = true;
        table->insert(entry);
    }
    return pv_line;
}

bool is_legit_input(char file, char rank)
{
    if(rank == '1' || rank == '2' || rank == '3' || rank == '4' || rank == '5' || rank == '6' || rank == '7' || rank == '8')
    if(file == 'a' || file == 'b' || file == 'c' || file == 'd' || file == 'e' || file == 'f' || file == 'g' || file == 'h')
    return true;
    
    return false;
}

int result(const BB* const original)//0=game on 1=white wins -1=black wins 2=draw
{
    int exception_state=exception_eval(original);//0=no_exception 1=stalmate 2=checkmate

    if(exception_state==0)
    {
        // Draw by threefold repetition: `history` (populated by
        // nicely_written_play()) already has *original as its last entry at
        // this point, so counting how many times this exact position (board +
        // side to move + castling rights + en passant, via the existing
        // are_equal()) appears in it naturally includes "now" as one
        // occurrence. Bounded to the tail since the last capture/pawn move,
        // same as minimax()'s own repetition window - nothing further back
        // could ever repeat. This is the real FIDE threefold rule, not
        // minimax()'s more aggressive search-time draw heuristic (which also
        // treats a single upcoming-cycle as drawish for pruning purposes) -
        // the two are deliberately different: one is the actual game result,
        // the other is a search approximation.
        int window_start = max(0, (int)history.size()-1-original->halfmoves_since_last_capture_or_pawn_move);
        int occurrences = 0;
        for(int i=(int)history.size()-1; i>=window_start; i--)
        {
            if(are_equal(&history[i], original))
            occurrences++;
        }
        if(occurrences>=3)
        {
            cout << "Draw by threefold repetition." << endl;
            return 2;//draw
        }
        return 0;//game on
    }
    cout << "Thee war was result: " << exception_state << endl;
    if(exception_state==1)
    return 2;//draw

    if(original->white_move)
    if(exception_state==2)
    return -1;//black wins
    if(!original->white_move)
    if(exception_state==2)
    return 1;//white wins

    cout << "Error there is no result" << endl;
    exit(1);
    return 50;
}

class PP //play parameters
{
    public:
    BB* original=0;
    BB* wfh=0;
    lookup_table* table=0;
    int depth=4;
    bool is_timed_move=0;//if true, engine moves use timed_engine_move() (iterative deepening against time_limit_seconds) instead of engine_move() (fixed depth)
    double time_limit_seconds=5.0;//per-move time budget used when is_timed_move is true
    bool colour=1;//which colour do you play?
    bool is_human_play=1;
    bool is_pretty_print=1;
    bool is_starting_pos_by_force=0;
    bool is_supposed_to_print_PGN=1;
    bool is_supposed_to_give_out_move=1;
    bool show_eval=0;
    WEIGHTS W=WEIGHTS_OG;/// weights for bith colours
    WEIGHTS W_white=WEIGHTS_OG;
    WEIGHTS W_black=WEIGHTS_OG;
    FILE* pipe=0;
    string name_of_python_script="display_board.py";
    chrono::milliseconds delay=chrono::milliseconds(0);
    bool print_Board=1;
    int max_game_lengh=100;
    string file_original ="weights.txt";
    int Number_of_games =10;
    int learning_rate=5;
    int probability_of_change=10;
};

string read_from_last_move(const bool col)
{
    char colour = col ? 'w' : 'b';
    
    string last_move="";
    while (last_move[5]!=colour || last_move.size()==0)
    {
        ifstream file;
        file.open("last_move.txt");
        this_thread::sleep_for(chrono::milliseconds(200));
        //cout << "Waiting" << endl;
        //cout << last_move << endl;
        getline(file,last_move);
        file.close();
        
    }
        ofstream file;
        file.open("last_move.txt");
        file << "";
        file.close();
    
    
    return last_move;

}

// Tracks, per search depth, how long that depth actually took to search the last
// (up to) two times it was reached, so timed_engine_move() can predict the cost of
// the next depth before committing to it. See DepthTimeStats::estimate().
struct DepthTimeStats
{
    // samples[d] = {most recent time at depth d, second-most-recent time at depth d}
    std::unordered_map<int, std::array<chrono::duration<double>,2>> samples;
    std::unordered_map<int, int> counts; // how many of the two slots above are filled (0,1,2)

    void record(int depth, chrono::duration<double> t)
    {
        auto& arr = samples[depth];
        arr[1] = arr[0];
        arr[0] = t;
        int& c = counts[depth];
        c = min(c+1,2);
    }

    // Weighted-average time for `depth` from its own history: 2/3 on the most recent
    // sample, 1/3 on the second-most-recent; full weight on the single sample if only
    // one is known. Returns false (out left untouched) if depth was never reached.
    bool estimate(int depth, chrono::duration<double>& out) const
    {
        auto it = counts.find(depth);
        if(it==counts.end() || it->second==0)
        return false;
        const auto& arr = samples.at(depth);
        if(it->second==1)
        out = arr[0];
        else
        out = arr[0]*(2.0/3.0) + arr[1]*(1.0/3.0);
        return true;
    }

    // Predicted time to search `depth`:
    //  1. its own weighted-average history, if depth was reached before;
    //  2. otherwise, geometric extrapolation: if both depth-1 and depth-2 have
    //     history, the search-tree size (and so search time) grows roughly by a
    //     constant factor per ply - the "effective branching factor" - so we take
    //     the ratio estimate(depth-1)/estimate(depth-2) actually observed for this
    //     position and project it one more ply forward. This adapts to how bushy
    //     the current position actually is instead of assuming a fixed constant;
    //  3. otherwise, if only depth-1 is known, fall back to a flat multiplier of 6
    //     - a rough effective branching factor for alpha-beta search with decent
    //     move ordering (real branching factor ~35, alpha-beta with good ordering
    //     gets close to its sqrt) - better than nothing, but only used this early;
    //  4. otherwise "unknown" (predicted as zero, i.e. always attempt it - used to
    //     bootstrap the very first depth of the very first move of a game).
    static constexpr double fallback_branching_factor = 6.0;
    chrono::duration<double> predict(int depth) const
    {
        chrono::duration<double> out(0.0);
        if(estimate(depth,out))
        return out;

        chrono::duration<double> prev(0.0), prev2(0.0);
        bool have_prev = estimate(depth-1,prev);
        if(have_prev && estimate(depth-2,prev2) && prev2.count()>0)
        return prev * (prev.count()/prev2.count());

        if(have_prev)
        return prev * fallback_branching_factor;

        return out; // 0.0 - no information yet, don't block the search
    }
};

class Play  : public initialize_FEN_to
{
    public :
       

/*
    int improve_weighs(BB*wfh, string file_original, int depth=1,bool show_eval=0, int Number_of_games =10 , bool print_Board =0,string name_of_python_script="", int learning_rate =10 , int max_game_lengh=150, int probability_of_change=100, chrono::milliseconds delay=chrono::milliseconds(0), BB* original=0,lookup_table* table=0)//max_game_lenght is ther beause we currently cannot detect daws by repetition
{
    int counter =0;
    bool OG_plays_white=1;
    int result;
    for(int i=0;i<Number_of_games;i++)
    {
        cout << "\nGame: " << i << endl;
        
        OG_plays_white=!OG_plays_white;
        
        WEIGHTS W_original;
        W_original.read_values_from_file(file_original);

        WEIGHTS W_contender=W_original;
        W_contender.change_values(learning_rate,!OG_plays_white,probability_of_change);
        if(print_Board)
        {
            if(OG_plays_white)
            cout << "Black ist the contender!" << endl;
            else
            cout << "White ist the contender!" << endl;
        }
        if(original==0)
        {
            if(name_of_python_script=="")
        {
            if(OG_plays_white)
            result = play_engine_from_starting_position(wfh,depth,show_eval,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_from_starting_position(wfh,depth,show_eval,W_contender,W_original,print_Board,max_game_lengh,table);
        }
        if(name_of_python_script!="")
        {
            if(OG_plays_white)
            result = play_engine_from_starting_position_with_pretty_print(name_of_python_script,wfh,depth,show_eval,delay,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_from_starting_position_with_pretty_print(name_of_python_script,wfh,depth,show_eval,delay,W_contender,W_original,print_Board,max_game_lengh,table);
        }

        }

        if(original!=0)
        {
            if(name_of_python_script=="")
        {
            if(OG_plays_white)
            result = play_engine(original,wfh,depth,show_eval,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine(original,wfh,depth,show_eval,W_contender,W_original,print_Board,max_game_lengh,table);
        }
        if(name_of_python_script!="")
        {
            if(OG_plays_white)
            result = play_engine_with_pretty_print(original,wfh,name_of_python_script,depth,show_eval,delay,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_with_pretty_print(original,wfh,name_of_python_script,depth,show_eval,delay,W_contender,W_original,print_Board,max_game_lengh,table);
        }

        }
        

        if(result==3)
        my_tournament.terminated_games++;
        if(result==2)
        my_tournament.draws++;
        if(result==1)
        my_tournament.white_wins++;
        if(result==-1)
        my_tournament.black_wins++;
        if(result==0)
        cout << "Error: no result" << endl;//this should be impossible anyway, but better safe than sorry

        if(OG_plays_white)
        if(result==-1)
        {
            cout << "Contender wins as black!" << endl;
            W_contender.print_values_to_file(file_original);
            W_contender.append_weights_to_File();
            counter++;
        }

        if(!OG_plays_white)
        if(result==1)
        {
            cout << "Contender wins as white!" << endl;
            W_contender.print_values_to_file(file_original);
            W_contender.append_weights_to_File();
            counter++;
        }

    }
    
    return counter;
}
    
    int improve_weighs_with_repect_to_W_OG(BB*wfh, string file_original, int depth=1,bool show_eval=0, int Number_of_games =10 , bool print_Board =0,string name_of_python_script="", int learning_rate =10 , int max_game_lengh=150, int probability_of_change=100, chrono::milliseconds delay=chrono::milliseconds(0),const BB*const  original=0,lookup_table* table=0)//max_game_lenght is ther beause we currently cannot detect daws by repetition
    {
        int counter =0;
        bool OG_plays_white=1;
        int result;
        WEIGHTS W_original=WEIGHTS_OG;
        for(int i=0;i<Number_of_games;i++)
        {
            cout << "\nGame: " << i << endl;
            
            OG_plays_white=!OG_plays_white;
            
            

            WEIGHTS W_contender;
            W_contender.read_values_from_file(file_original);
            W_contender.change_values(learning_rate,!OG_plays_white,probability_of_change);
            if(print_Board)
            {
                if(OG_plays_white)
                cout << "Black ist the contender!" << endl;
                else
                cout << "White ist the contender!" << endl;
            }
            if(print_Board)
        {
            if(OG_plays_white)
            cout << "Black ist the contender!" << endl;
            else
            cout << "White ist the contender!" << endl;
        }
        if(original==0)
        {
            if(name_of_python_script=="")
        {
            if(OG_plays_white)
            result = play_engine_from_starting_position(wfh,depth,show_eval,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_from_starting_position(wfh,depth,show_eval,W_contender,W_original,print_Board,max_game_lengh,table);
        }
        if(name_of_python_script!="")
        {
            if(OG_plays_white)
            result = play_engine_from_starting_position_with_pretty_print(name_of_python_script,wfh,depth,show_eval,delay,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_from_starting_position_with_pretty_print(name_of_python_script,wfh,depth,show_eval,delay,W_contender,W_original,print_Board,max_game_lengh,table);
        }

        }

        if(original!=0)
        {
            if(name_of_python_script=="")
        {
            if(OG_plays_white)
            result = play_engine(original,wfh,depth,show_eval,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine(original,wfh,depth,show_eval,W_contender,W_original,print_Board,max_game_lengh,table);
        }
        if(name_of_python_script!="")
        {
            if(OG_plays_white)
            result = play_engine_with_pretty_print(original,wfh,name_of_python_script,depth,show_eval,delay,W_original,W_contender,print_Board,max_game_lengh,table);
            else
            result = play_engine_with_pretty_print(original,wfh,name_of_python_script,depth,show_eval,delay,W_contender,W_original,print_Board,max_game_lengh,table);
        }

        }

            
            if(result==3)
            my_tournament.terminated_games++;
            if(result==2)
            my_tournament.draws++;
            if(result==1)
            my_tournament.white_wins++;
            if(result==-1)
            my_tournament.black_wins++;
            if(result==0)
            cout << "Error: no result" << endl;//this should be impossible anyway, but better safe than sorry

            if(OG_plays_white)
            if(result==-1)
            {
                cout << "Contender wins as black!" << endl;
                W_contender.print_values_to_file(file_original);
                W_contender.append_weights_to_File();
                counter++;
            }

            if(!OG_plays_white)
            if(result==1)
            {
                cout << "Contender wins as white!" << endl;
                W_contender.print_values_to_file(file_original);
                W_contender.append_weights_to_File();
                counter++;
            }

        }
        
        return counter;
    }
    */
    
    public:
    PP p;
    // Per-depth timing history used by timed_engine_move() to predict how long the
    // next depth will take. Keeps up to the last 2 times each depth was actually
    // reached, weighted 2/3 most recent + 1/3 second most recent. Kept here (in the
    // caller of timed_engine_move) rather than inside the search itself so the
    // estimate accumulates across the whole game. White and black get separate
    // histories since their positions/branching factor over a game can diverge.
    DepthTimeStats time_stats_white;
    DepthTimeStats time_stats_black;
    //these three should be the only non special(zb checkmating line) play functions
    void human_move(BB *original, BB* wfh, bool pretty_print=0)
    {
            
            ofstream temp_file;
            
            
            
            auto result = all_moves(original,wfh);
            int number_of_new_moves = std::get<0>(result);
            bool aligns_with_goal=0;
            int index_of_alignment=0;
            while (!aligns_with_goal)
            {
            if(pretty_print)
            {
                temp_file.open("last_move.txt");
                temp_file << "";
                temp_file.close();
            }
                string move;
                char file;
                char rank;
                int start=0,end=0;
                if(!pretty_print)
                {
                    do
                    {
                        cout<<"\nEnter starting file, rank: ";
                        cin>>file;
                        cin>>rank;
                    } while (!is_legit_input(file, rank));
                    
                    
                    rank--;
                    start=8*(rank-'0')+(file-'a');
                    
                    do
                    {
                        cout<<"Enter ending file, rank: ";
                        cin>>file;
                        cin>>rank;
                    } while (!is_legit_input(file, rank));
                    rank--;
                    end=8*(rank-'0')+(file-'a');
                }
                else
                {
                    move = read_from_last_move(p.colour);
                    
                    cout << "The move was: " << move << endl;
                    start=8*(move[1]-'1')+(move[0]-'a');
                    end=8*(move[3]-'1')+(move[2]-'a');
                    if(original->castle[original->white_move][0])//queen side
                    if(start==4+8*7*(!original->white_move) )// king of relevant colout
                    if(end==2+8*7*(!original->white_move) || end==1+8*7*(!original->white_move) || end==0+8*7*(!original->white_move))//count all moves on the backrank
                    {
                        end=2+8*7*(!original->white_move);
                    }
                    if(original->castle[original->white_move][1])//king side
                    if(start==4+8*7*(!original->white_move) )// king of relevant colout
                    if(end==6+8*7*(!original->white_move) || end==7+8*7*(!original->white_move))//count all moves on the backrank
                    {
                        end=6+8*7*(!original->white_move);
                    }
                }
                
                BB temp = *original;
                int piece_type=0;
                for(int i=0;i<12;i++)
                {
                    if(temp.Board[i] & (1ULL<<start))
                    {
                        piece_type=i;
                        break;
                    }
                }

                temp.Board[piece_type] &= ~(1ULL<<start);
                temp.Board[piece_type] |= (1ULL<<end);
                bool is_promotion=0;
                int promotion_piece=-1;
                if(piece_type==0 && end >= 8*7 || piece_type==6 && end <= 7)
                {
                    is_promotion=1;
                    temp.Board[piece_type] &= ~(1ULL<<end);
                    char promotion;
                    if(!pretty_print)
                    do
                    {
                        cout<<"Enter promotion piece: ";
                        cin>>promotion;
                    } while (promotion!='q' && promotion!='r' && promotion!='b' && promotion!='n');
                    else
                    promotion=move[4];
                    if(promotion=='q' || promotion=='Q')
                    promotion_piece=4+6*(!temp.white_move);
                    if(promotion=='r' || promotion=='R')
                    promotion_piece=1+6*(!temp.white_move);
                    if(promotion=='b' || promotion=='B')
                    promotion_piece=3+6*(!temp.white_move);
                    if(promotion=='n' || promotion=='N')
                    promotion_piece=2+6*(!temp.white_move);
                    temp.Board[promotion_piece] |= (1ULL<<end);
                }
                
                if(!is_promotion)
                for(int i=0;i<number_of_new_moves;i++)
                if(wfh[i].Board[piece_type]==temp.Board[piece_type])
                {
                    index_of_alignment=i;
                    aligns_with_goal=1;
                    break;
                } 
                if(is_promotion)
                for(int i=0;i<number_of_new_moves;i++)
                if(wfh[i].Board[promotion_piece]==temp.Board[promotion_piece])
                {
                    index_of_alignment=i;
                    aligns_with_goal=1;
                    break;
                }
                if(!aligns_with_goal)
                {
                    cout<<"Invalid move. Try again."<<endl;
                    cout <<"The suggested move was: " << get_UCI(original,&temp) << endl;
                    cout << "which corresponds to: " << endl;
                    print(temp.Board);
                }        
            }
            cout << "The move was: " << get_UCI(original,wfh+index_of_alignment) << "aka." << get_move(original,wfh+index_of_alignment) << endl;
            copy_BB(wfh+index_of_alignment,original);
    }

    int engine_move_old(BB* original, BB* wfh, bool pretty_print=0,int depth=1, WEIGHTS W=WEIGHTS_OG,lookup_table* table=0,FILE* pipe=0)
    {
        int alpha=INT_MIN,beta=INT_MAX;
        int best_move_index=0;
        
        bool table_provides_eval=0;
        Move tabulated_move;

        PV_Line pv_line =PV_Line(original->white_move ? INT_MIN : INT_MAX);//initialize with worst possible value for the player to movey
        PV_Line tt_hint;
        bool is_tt_hint_found=0;
        if(table)
        {
            TT_readout readout = table->is_retrivable_eval(original,depth);
            if(readout.is_found)
            {
                if(depth<=readout.pv_line.depth)
                {
                    if(readout.pv_line.bound_type==0 && readout.pv_line.current_lenght>0)//exact
                    {
                        table_provides_eval=1;
                        pv_line=readout.pv_line;
                        pv_line.depth=readout.pv_line.depth;
                        tabulated_move=readout.pv_line.moves[0];
                    }
                    
                    else if(depth==readout.pv_line.depth)//only on exact depth alpha and beta can be updated
                    {
                        //update alpha and beta based on the bound type
                        if(readout.pv_line.bound_type == -1) // lower bound
                        alpha = max(alpha, readout.pv_line.eval);
                        else if(readout.pv_line.bound_type == 1) // upper bound
                        beta = min(beta, readout.pv_line.eval);
                    }
                }
                tt_hint=readout.pv_line;
                is_tt_hint_found=1;
            }
        }
        auto result = all_moves(original,wfh);
        int number_of_new_moves = std::get<0>(result);
        vector<Move> moves = std::get<1>(result);
        if(number_of_new_moves==1)
        {
            copy_BB(wfh,original);
            return INT_MAX/2;
        }
        vector<int> indices = sorting_moves(wfh,moves,number_of_new_moves,original->white_move,&tt_hint,0,W);//descending
        
        if(table_provides_eval)//may need rework
            {
                int i=indices[0];
                if(pipe)
                {
                    const int displayed_length = pv_line.current_lenght;
                    //min(pv_line.depth,pv_line.current_lenght);
                    vector<Move> pv_moves(pv_line.moves,
                                          pv_line.moves + displayed_length);
                    write_to_python_script(pipe, "PV depth=" + to_string(depth) +
                                                " eval=" + to_string(pv_line.eval) +
                                                " " + moves_to_PGN(*original, pv_moves));
                }
                copy_BB(wfh+i,original);
                return pv_line.eval;
            }
        prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
        //cout << "Engine move calls assign_depth" << endl;
        const int alpha_0 = alpha, beta_0 = beta;
        PV_Line best_candidate_PV_line;
        bool best_candidate_PV_line_initialized=0;
        for(int i=indices[0],k=0;k<number_of_new_moves;k++,i=indices[k])
        {
            Move move = moves[indices[k]];
            int depth_to_use=depth-1;
            if(is_good_capture(original,move.from,move.to,move.is_en_passant,W))
            depth_to_use++;
            PV_Line candidate_PV_line;
            for(int j=0;j<=depth_to_use;j++)
            {
                candidate_PV_line = minimax(wfh+i,wfh+number_of_new_moves,j,W,alpha,beta,table);
            }
            int eval = candidate_PV_line.eval;
            if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
            eval++;
            if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
            eval--;
            if(original->white_move)
            {
                if(eval>pv_line.eval)
                {
                    best_move_index=i;
                    pv_line = PV_Line(move,depth,&candidate_PV_line);
                    best_candidate_PV_line = candidate_PV_line;
                    best_candidate_PV_line_initialized=1;
                }
                pv_line.eval=max(pv_line.eval,eval);
                alpha=max(alpha,eval);
            }
            else 
            {
                if(eval<pv_line.eval)
                {
                    best_move_index=i;
                    pv_line = PV_Line(move,depth,&candidate_PV_line);
                    best_candidate_PV_line = candidate_PV_line;
                    best_candidate_PV_line_initialized=1;
                }

                pv_line.eval=min(pv_line.eval,eval);
                beta=min(beta,eval);
            }
            if(beta<=alpha)
            {
                pruned_moves+=number_of_new_moves-k-1;
                break;
            }
        }
        //now correct for fail low
        if(pv_line.eval <= alpha_0)
            pv_line.bound_type = 1;   // upper bound (fail-low)
        else if(pv_line.eval >= beta_0)
            pv_line.bound_type = -1;  // lower bound (fail-high)
        else
            pv_line.bound_type = 0;   // exact
        if(table)
        {
            TT_entry entry;
            entry.board = *original;
            entry.zobrist_hash=original->zobrist_hash;
            entry.pv_line = pv_line;
            entry.initialized=1;
            table->insert(entry);
        }
        if(pipe)
        {
            const int displayed_length = min(pv_line.depth,
                                              pv_line.current_lenght);
            vector<Move> pv_moves(pv_line.moves,
                                  pv_line.moves + displayed_length);
            write_to_python_script(pipe, "PV depth=" + to_string(depth) +
                                        " eval=" + to_string(pv_line.eval) +
                                        " " + moves_to_PGN(*original, pv_moves));
        }
        //now print the engine line
        if(best_candidate_PV_line_initialized)
        cout << "The best candidate PV line was initialized!" << endl;
        else
        cout << "The best candidate PV line was NOT initialized!" << endl;
        cout << "The best candidate PV line is: " << endl;
        for(int i=0;i<best_candidate_PV_line.current_lenght;i++)
        {
            cout << "best_candidate_PV_line.depth: " << best_candidate_PV_line.depth << endl;
            Move move = best_candidate_PV_line.moves[i];
            int starting_rank = move.from/8;
            char starting_file = 'a' + (move.from % 8);
            int ending_rank = move.to/8;
            char ending_file = 'a' + (move.to % 8);
            cout << starting_file << starting_rank+1 << " " << ending_file << ending_rank+1 << std::endl;
        }
        cout << "The engine line is: " << endl;
        
        for(int i=0;i<pv_line.current_lenght;i++)
        {
            cout << "pv_line.depth: " << pv_line.depth << endl;
            Move move = pv_line.moves[i];
            int starting_rank = move.from/8;
            char starting_file = 'a' + (move.from % 8);
            int ending_rank = move.to/8;
            char ending_file = 'a' + (move.to % 8);
            cout << starting_file << starting_rank+1 << " " << ending_file << ending_rank+1 << std::endl;
            
        }
        const int displayed_length = min(pv_line.depth,
                          pv_line.current_lenght);
        vector<Move> pv_moves(pv_line.moves,
                      pv_line.moves + displayed_length);
        cout << "Pretty engine line: " << moves_to_PGN(*original, pv_moves) << std::endl;
        cout << "Evaluation after this move: " << pv_line.eval << std::endl;
        copy_BB(wfh+best_move_index,original);
        return pv_line.eval;
    }

    // Thin wrapper around minimax(): minimax already returns the root PV, so the only
    // thing kept here is the outer iterative-deepening loop, which feeds the lookup
    // table forward from shallow to deep so sorting_moves gets a good PV-move hint at
    // every depth. Everything minimax already does internally (root TT short-circuit,
    // alpha-beta, TT insertion) is not duplicated here.
    int engine_move(BB* original, BB* wfh, bool pretty_print=0,int depth=1, WEIGHTS W=WEIGHTS_OG,lookup_table* table=0,FILE* pipe=0)
    {
        auto result = all_moves(original,wfh);
        int number_of_new_moves = std::get<0>(result);
        vector<Move> moves = std::get<1>(result);
        if(number_of_new_moves==1)
        {
            copy_BB(wfh,original);
            return INT_MAX/2;
        }

        // Repetition/cycle detection context for minimax(): built once (the
        // CuckooCycleTable's population and the path array's default-construction
        // both only happen on the very first call, thanks to `static`) and reseeded
        // from the real game's `history` on every move. Only the tail bounded by
        // halfmoves_since_last_capture_or_pawn_move is copied - nothing further
        // back could ever be part of a repeat, and `history` itself can outgrow
        // MAX_SEARCH_PLY over a long game.
        static const CuckooCycleTable cycle_table;
        static BB path_history[MAX_SEARCH_PLY];
        int seed_count = min({(int)history.size(), original->halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
        for(int i=0;i<seed_count;i++)
        path_history[i] = history[history.size()-seed_count+i];
        int ply = (seed_count>0) ? seed_count-1 : 0;

        PV_Line pv_line;
        for(int d=1;d<=depth;d++)
        {
            pv_line = minimax(original,wfh+number_of_new_moves,d,W,INT_MIN,INT_MAX,table,path_history,ply,&cycle_table);
            if(pipe)
            {
                const int displayed_length = min(pv_line.depth,pv_line.current_lenght);
                vector<Move> pv_moves(pv_line.moves,pv_line.moves+displayed_length);
                write_to_python_script(pipe, "PV depth=" + to_string(d) +
                                            " eval=" + to_string(pv_line.eval) +
                                            " " + moves_to_PGN(*original, pv_moves));
            }
            bool is_proven_checkmate = (pv_line.eval <= INT_MIN + max_mating_seq) || (pv_line.eval >= INT_MAX - max_mating_seq);
            if(is_proven_checkmate)
            d = depth+1;
            //std::cin.get(); // Wait for user input before proceeding to the next depth
        }

        int best_move_index=0;
        for(int i=0;i<number_of_new_moves;i++)
        {
            if(moves[i]==pv_line.moves[0])
            {
                best_move_index=i;
                break;
            }
        }

        if(pretty_print)
        {
            const int displayed_length = min(pv_line.depth,pv_line.current_lenght);
            vector<Move> pv_moves(pv_line.moves,pv_line.moves+displayed_length);
            cout << "Engine line: " << moves_to_PGN(*original, pv_moves) << std::endl;
            cout << "Evaluation after this move: " << pv_line.eval << std::endl;
        }

        copy_BB(wfh+best_move_index,original);
        return pv_line.eval;
    }

    // Same as engine_move(), but instead of always deepening to a fixed `depth`,
    // it deepens iteratively until either a proven mate is found or `stats` predicts
    // the next depth would push total time over time_limit, in which case that depth
    // is skipped and the search stops. `stats` is owned by the caller (see
    // time_stats_white/time_stats_black) and persists across moves so predictions
    // improve over the course of the game. Always completes at least depth 1, so it
    // always returns a legal move.
    int timed_engine_move(BB* original, BB* wfh, DepthTimeStats& stats, bool pretty_print=0, chrono::duration<double> time_limit=chrono::duration<double>(5.0), WEIGHTS W=WEIGHTS_OG,lookup_table* table=0,FILE* pipe=0)
    {
        auto result = all_moves(original,wfh);
        int number_of_new_moves = std::get<0>(result);
        vector<Move> moves = std::get<1>(result);
        if(number_of_new_moves==1)
        {
            copy_BB(wfh,original);
            return INT_MAX/2;
        }

        static const CuckooCycleTable cycle_table;
        static BB path_history[MAX_SEARCH_PLY];
        int seed_count = min({(int)history.size(), original->halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
        for(int i=0;i<seed_count;i++)
        path_history[i] = history[history.size()-seed_count+i];
        int ply = (seed_count>0) ? seed_count-1 : 0;

        auto search_start = chrono::steady_clock::now();
        chrono::duration<double> elapsed(0.0);
        chrono::duration<double> last_depth_duration(0.0);

        // Describes, for debug printing, exactly how stats.predict(depth) arrived at
        // its number: own weighted history (and the raw sample(s) it came from), the
        // 30x-lower-depth fallback, or "no info at all".
        auto describe_prediction = [&](int depth) -> string
        {
            chrono::duration<double> e;
            auto it = stats.counts.find(depth);
            if(it!=stats.counts.end() && it->second>0)
            {
                const auto& arr = stats.samples.at(depth);
                if(it->second==1)
                return "own history, 1 sample: " + to_string(arr[0].count()) + "s -> predicted " + to_string(arr[0].count()) + "s";
                stats.estimate(depth,e);
                return "own history, 2 samples: most-recent=" + to_string(arr[0].count()) + "s (weight 2/3), second-most-recent="
                     + to_string(arr[1].count()) + "s (weight 1/3) -> predicted " + to_string(e.count()) + "s";
            }
            chrono::duration<double> prev, prev2;
            bool have_prev = stats.estimate(depth-1,prev);
            if(have_prev && stats.estimate(depth-2,prev2) && prev2.count()>0)
            {
                double ratio = prev.count()/prev2.count();
                return "no history for depth " + to_string(depth) + ", extrapolating growth ratio from depth "
                     + to_string(depth-1) + " (" + to_string(prev.count()) + "s) / depth " + to_string(depth-2)
                     + " (" + to_string(prev2.count()) + "s) = " + to_string(ratio) + "x -> predicted "
                     + to_string((prev*ratio).count()) + "s";
            }
            if(have_prev)
            return "no history for depth " + to_string(depth) + " or depth " + to_string(depth-2)
                 + ", using fallback " + to_string(DepthTimeStats::fallback_branching_factor) + "x depth " + to_string(depth-1)
                 + "'s estimate (" + to_string(prev.count()) + "s) -> predicted " + to_string((prev*DepthTimeStats::fallback_branching_factor).count()) + "s";
            return "no history at all yet -> predicted 0s (unblocked)";
        };

        PV_Line pv_line;
        for(int d=1;;d++)
        {
            // Depth 1 always runs (need at least one move); for deeper iterations,
            // skip (stop deepening) if the predicted cost of this depth would blow
            // the remaining time budget.
            if(d>1)
            {
                chrono::duration<double> predicted = stats.predict(d);
                if(pretty_print)
                cout << "Depth " << d << " prediction: " << describe_prediction(d)
                     << " | elapsed so far " << elapsed.count() << "s, budget " << time_limit.count() << "s" << endl;
                if(elapsed + predicted > time_limit)
                {
                    if(pretty_print)
                    cout << "Skipping depth " << d << ": elapsed(" << elapsed.count() << "s) + predicted("
                         << predicted.count() << "s) exceeds budget(" << time_limit.count() << "s)" << endl;
                    break;
                }
            }

            auto depth_start = chrono::steady_clock::now();
            pv_line = minimax(original,wfh+number_of_new_moves,d,W,INT_MIN,INT_MAX,table,path_history,ply,&cycle_table);
            last_depth_duration = chrono::steady_clock::now() - depth_start;
            elapsed = chrono::steady_clock::now() - search_start;

            // A time that's faster than the shallower depth's own average is a search
            // that can only be that quick because of a TT hit or similar shortcut, not
            // because depth d is genuinely cheaper than depth d-1 - discard it instead
            // of feeding it into stats, since it would otherwise drag the prediction
            // down and cause a later depth to be under-budgeted. No such check exists
            // for depth 1 - there's no depth 0 to compare it against.
            bool looks_like_outlier = false;
            string outlier_reason;
            if(d>1)
            {
                chrono::duration<double> prev_avg;
                if(stats.estimate(d-1,prev_avg) && last_depth_duration < prev_avg)
                {
                    looks_like_outlier = true;
                    outlier_reason = "faster than depth " + to_string(d-1) + "'s average (" + to_string(prev_avg.count()) + "s)";
                }
            }
            if(!looks_like_outlier)
            stats.record(d,last_depth_duration);

            if(pretty_print)
            {
                cout << "Depth " << d << " actually took " << last_depth_duration.count() << "s, total elapsed " << elapsed.count() << "s";
                if(looks_like_outlier)
                cout << " (outlier - " << outlier_reason << " - not recorded into stats)";
                cout << endl;
            }

            if(pipe)
            {
                const int displayed_length = min(pv_line.depth,pv_line.current_lenght);
                vector<Move> pv_moves(pv_line.moves,pv_line.moves+displayed_length);
                write_to_python_script(pipe, "PV depth=" + to_string(d) +
                                            " eval=" + to_string(pv_line.eval) +
                                            " " + moves_to_PGN(*original, pv_moves));
            }

            bool is_proven_checkmate = (pv_line.eval <= INT_MIN + max_mating_seq) || (pv_line.eval >= INT_MAX - max_mating_seq);
            if(is_proven_checkmate)
            {
                if(pretty_print)
                cout << "Stopping: depth " << d << " found a proven mate." << endl;
                break;
            }
        }

        int best_move_index=0;
        for(int i=0;i<number_of_new_moves;i++)
        {
            if(moves[i]==pv_line.moves[0])
            {
                best_move_index=i;
                break;
            }
        }

        if(pretty_print)
        {
            const int displayed_length = min(pv_line.depth,pv_line.current_lenght);
            vector<Move> pv_moves(pv_line.moves,pv_line.moves+displayed_length);
            cout << "Engine line: " << moves_to_PGN(*original, pv_moves) << std::endl;
            cout << "Evaluation after this move: " << pv_line.eval << std::endl;
            cout << "Time spent: " << elapsed.count() << "s (limit " << time_limit.count() << "s), reached depth " << pv_line.depth << std::endl;
        }

        copy_BB(wfh+best_move_index,original);
        return pv_line.eval;
    }

    int engine_move_for_time_testing(BB* original, BB* wfh, bool pretty_print=0,int depth=1, WEIGHTS W=WEIGHTS_OG,lookup_table* table=0,FILE* pipe=0)
    {
        int alpha=INT_MIN,beta=INT_MAX;
        int best_move_index=0;
        
        bool table_provides_eval=0;
        Move tabulated_move;

        PV_Line pv_line =PV_Line(original->white_move ? INT_MIN : INT_MAX);//initialize with worst possible value for the player to movey
        PV_Line tt_hint;
        bool is_tt_hint_found=0;
        if(table)
        {
            TT_readout readout = table->is_retrivable_eval(original,depth);
            if(readout.is_found)
            {
                if(depth<=readout.pv_line.depth)
                {
                    if(readout.pv_line.bound_type==0 && readout.pv_line.current_lenght>0)//exact
                    {
                        table_provides_eval=1;
                        pv_line=readout.pv_line;
                        pv_line.depth=readout.pv_line.depth;
                        tabulated_move=readout.pv_line.moves[0];
                    }
                    
                    else if(depth==readout.pv_line.depth)//only on exact depth alpha and beta can be updated
                    {
                        //update alpha and beta based on the bound type
                        if(readout.pv_line.bound_type == -1) // lower bound
                        alpha = max(alpha, readout.pv_line.eval);
                        else if(readout.pv_line.bound_type == 1) // upper bound
                        beta = min(beta, readout.pv_line.eval);
                    }
                }
                tt_hint=readout.pv_line;
                is_tt_hint_found=1;
            }
        }
        auto result = all_moves(original,wfh);
        int number_of_new_moves = std::get<0>(result);
        vector<Move> moves = std::get<1>(result);
        if(number_of_new_moves==1)
        {
            copy_BB(wfh,original);
            return INT_MAX/2;
        }
        vector<int> indices = sorting_moves(wfh,moves,number_of_new_moves,original->white_move,&tt_hint,0,W);//descending
        
        if(table_provides_eval)
            {
                int i=indices[0];
                if(pipe)
                {
                    const int displayed_length = pv_line.current_lenght;
                    //min(pv_line.depth,pv_line.current_lenght);
                    vector<Move> pv_moves(pv_line.moves,
                                          pv_line.moves + displayed_length);
                    write_to_python_script(pipe, "PV depth=" + to_string(depth) +
                                                " eval=" + to_string(pv_line.eval) +
                                                " " + moves_to_PGN(*original, pv_moves));
                }
                copy_BB(wfh+i,original);
                return pv_line.eval;
            }
        prunable_moves_total+=number_of_new_moves-1;//analizing how efficient pruning is.
        //cout << "Engine move calls assign_depth" << endl;
        const int alpha_0 = alpha, beta_0 = beta;
        PV_Line best_candidate_PV_line;
        bool best_candidate_PV_line_initialized=0;
        for(int i=indices[0],k=0;k<number_of_new_moves;k++,i=indices[k])
        {
            Move move = moves[indices[k]];
            int depth_to_use=depth-1;
            if(captures_more_valuable_piece(original,wfh+i,W))
            depth_to_use++;
            PV_Line candidate_PV_line;
            // for(int j=depth_to_use;j<=depth_to_use;j++)
            // candidate_PV_line = minimax(wfh+i,wfh+number_of_new_moves,j,W,alpha,beta,table);
            printf("Evaluating move %d/%d: %s\n", k + 1, number_of_new_moves, get_UCI(original, wfh + i).c_str());
            print(wfh[i].Board);
            //time the minimax calls with and without lookup table
            auto start_time = chrono::high_resolution_clock::now();
            PV_Line pv_line_without_lookup_table = minimax(wfh+i,wfh+number_of_new_moves,depth_to_use,W,alpha,beta,NULL);
            auto end_time = chrono::high_resolution_clock::now();
            auto duration_without_lookup = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
            auto start_time_with_lookup = chrono::high_resolution_clock::now();
            PV_Line pv_line_with_lookup_table = minimax(wfh+i,wfh+number_of_new_moves,depth_to_use,W,alpha,beta,table);
            auto end_time_with_lookup = chrono::high_resolution_clock::now();
            auto duration_with_lookup = chrono::duration_cast<chrono::milliseconds>(end_time_with_lookup - start_time_with_lookup).count();
            
            //now delte the contents of the lookup table to see the difference
            table->reset();
            auto start_time_with_lookup_v2 = chrono::high_resolution_clock::now();
            PV_Line pv_line_with_lookup_table_v2 = minimax(wfh+i,wfh+number_of_new_moves,depth_to_use,W,alpha,beta,table);
            auto end_time_with_lookup_v2 = chrono::high_resolution_clock::now();
            auto duration_with_lookup_v2 = chrono::duration_cast<chrono::milliseconds>(end_time_with_lookup_v2 - start_time_with_lookup_v2).count();
            
            //now a third time first delete the tables contents, then call minimax with iterativly increasing depth
            table->reset();
            auto start_time_with_lookup_v3 = chrono::high_resolution_clock::now();
            PV_Line pv_line_with_lookup_table_v3;
            for(int j=0;j<=depth_to_use;j++)
            {
                pv_line_with_lookup_table_v3 = minimax(wfh+i,wfh+number_of_new_moves,j,W,alpha,beta,table);
            }
            auto end_time_with_lookup_v3 = chrono::high_resolution_clock::now();
            auto duration_with_lookup_v3 = chrono::duration_cast<chrono::milliseconds>(end_time_with_lookup_v3 - start_time_with_lookup_v3).count();
            cout << "Time taken without lookup table: " << duration_without_lookup << " ms" << endl;
            cout << "Time taken with lookup table (with lookup table filled from previous moves): " << duration_with_lookup << " ms" << endl;
            cout << "Time taken with lookup table v2 (no precomputed table): " << duration_with_lookup_v2 << " ms" << endl;
            cout << "Time taken with lookup table v3 (iterative deepening): " << duration_with_lookup_v3 << " ms" << endl;


            cout << "Eval and bound type without lookup table: " << pv_line_without_lookup_table.eval << ", bound type: " << pv_line_without_lookup_table.bound_type << endl;
            cout << "Eval and bound type with lookup table: " << pv_line_with_lookup_table.eval << ", bound type: " << pv_line_with_lookup_table.bound_type << endl;
            cout << "Eval and bound type with lookup table v2: " << pv_line_with_lookup_table_v2.eval << ", bound type: " << pv_line_with_lookup_table_v2.bound_type << endl;
            cout << "Eval and bound type with lookup table v3: " << pv_line_with_lookup_table_v3.eval << ", bound type: " << pv_line_with_lookup_table_v3.bound_type << endl;
            cin.get(); // Wait for user input before proceeding
            candidate_PV_line = pv_line_with_lookup_table;
            int eval = candidate_PV_line.eval;
            if(eval<INT_MIN+max_mating_seq)//this assures the quickest mate
            eval++;
            if(eval>INT_MAX-max_mating_seq)//this assures the quickest mate
            eval--;
            if(original->white_move)
            {
                if(eval>pv_line.eval)
                {
                    best_move_index=i;
                    pv_line = PV_Line(move,depth,&candidate_PV_line);
                    best_candidate_PV_line = candidate_PV_line;
                    best_candidate_PV_line_initialized=1;
                }
                pv_line.eval=max(pv_line.eval,eval);
                alpha=max(alpha,eval);
            }
            else 
            {
                if(eval<pv_line.eval)
                {
                    best_move_index=i;
                    pv_line = PV_Line(move,depth,&candidate_PV_line);
                    best_candidate_PV_line = candidate_PV_line;
                    best_candidate_PV_line_initialized=1;
                }

                pv_line.eval=min(pv_line.eval,eval);
                beta=min(beta,eval);
            }
            if(beta<=alpha)
            {
                pruned_moves+=number_of_new_moves-k-1;
                break;
            }
        }
        //now correct for fail low
        if(pv_line.eval <= alpha_0)
            pv_line.bound_type = 1;   // upper bound (fail-low)
        else if(pv_line.eval >= beta_0)
            pv_line.bound_type = -1;  // lower bound (fail-high)
        else
            pv_line.bound_type = 0;   // exact
        if(table)
        {
            TT_entry entry;
            entry.board = *original;
            entry.zobrist_hash=original->zobrist_hash;
            entry.initialized = true;
            entry.pv_line = pv_line;
            table->insert(entry);
        }
        if(pipe)
        {
            const int displayed_length = min(pv_line.depth,
                                              pv_line.current_lenght);
            vector<Move> pv_moves(pv_line.moves,
                                  pv_line.moves + displayed_length);
            write_to_python_script(pipe, "PV depth=" + to_string(depth) +
                                        " eval=" + to_string(pv_line.eval) +
                                        " " + moves_to_PGN(*original, pv_moves));
        }
        //now print the engine line
        if(best_candidate_PV_line_initialized)
        cout << "The best candidate PV line was initialized!" << endl;
        else
        cout << "The best candidate PV line was NOT initialized!" << endl;
        cout << "The best candidate PV line is: " << endl;
        for(int i=0;i<best_candidate_PV_line.current_lenght;i++)
        {
            cout << "best_candidate_PV_line.depth: " << best_candidate_PV_line.depth << endl;
            Move move = best_candidate_PV_line.moves[i];
            int starting_rank = move.from/8;
            char starting_file = 'a' + (move.from % 8);
            int ending_rank = move.to/8;
            char ending_file = 'a' + (move.to % 8);
            cout << starting_file << starting_rank+1 << " " << ending_file << ending_rank+1 << std::endl;
        }
        cout << "The engine line is: " << endl;
        
        for(int i=0;i<pv_line.current_lenght;i++)
        {
            cout << "pv_line.depth: " << pv_line.depth << endl;
            Move move = pv_line.moves[i];
            int starting_rank = move.from/8;
            char starting_file = 'a' + (move.from % 8);
            int ending_rank = move.to/8;
            char ending_file = 'a' + (move.to % 8);
            cout << starting_file << starting_rank+1 << " " << ending_file << ending_rank+1 << std::endl;
            
        }
        const int displayed_length = min(pv_line.depth,
                          pv_line.current_lenght);
        vector<Move> pv_moves(pv_line.moves,
                      pv_line.moves + displayed_length);
        cout << "Pretty engine line: " << moves_to_PGN(*original, pv_moves) << std::endl;
        cout << "Evaluation after this move: " << pv_line.eval << std::endl;
        copy_BB(wfh+best_move_index,original);
        return pv_line.eval;
    }

    int nicely_written_play() // 1 means white wins, -1 means black wins, 2 means stalemate /draw, 3 means terminated game
    {
        int game_result=0;
        int game_lenght=0;
        int current_eval=0;
        
        BB temp;
        BB store_original;
        if(!p.is_starting_pos_by_force)
        temp=*(p.original);
        else
        initialize_FEN_to::Standartboard(temp.Board);

        
        BB* original=&temp;
        castling_rights(original);
        if(p.is_pretty_print)
        p.pipe = open_python_script(p.name_of_python_script,get_FEN(*original));
        if(take_history)
        history.push_back(*original);
        while(game_lenght++<p.max_game_lengh || p.is_human_play)
        {
            number_of_half_moves++;
            auto temp = all_moves(original,p.wfh+200);
            int number_of_new_moves = std::get<0>(temp);
            Move* moves = std::get<1>(temp).data();
            print(original->Board);
            cout << endl << get_FEN(*original) << endl;
            bool WM=original->white_move;
            if(!(p.colour==WM && p.is_human_play)&& p.show_eval)//if its an engine move
            interpret_eval(current_eval);
            
            
            store_original=*original;

            int tt_insertions_before=0, tt_succ_before=0, tt_attempted_before=0;
            if(p.table)
            {
                tt_insertions_before = p.table->number_of_inserions;
                tt_succ_before = p.table->number_of_succ_readouts;
                tt_attempted_before = p.table->number_of_attemted_readouts;
            }

            //time the engine move
            auto start_time = chrono::high_resolution_clock::now();
            if(p.colour==WM && p.is_human_play)
            {
                if(!p.is_pretty_print)
                human_move(original,p.wfh,p.show_eval);
                else
                human_move(original,p.wfh,p.show_eval);
            }
            else if(p.colour!=WM && p.is_human_play)
            {
                DepthTimeStats& stats = WM ? time_stats_white : time_stats_black;
                current_eval = p.is_timed_move
                    ? timed_engine_move(original,p.wfh,stats,p.show_eval,chrono::duration<double>(p.time_limit_seconds),p.W,p.table,p.pipe)
                    : engine_move(original,p.wfh,p.show_eval,p.depth,p.W,p.table,p.pipe);
            }
            else if(WM)
            {
                current_eval = p.is_timed_move
                    ? timed_engine_move(original,p.wfh,time_stats_white,p.show_eval,chrono::duration<double>(p.time_limit_seconds),p.W_white,p.table,p.pipe)
                    : engine_move(original,p.wfh,p.show_eval,p.depth,p.W_white,p.table,p.pipe);
            }
            else if(!WM)
            {
                current_eval = p.is_timed_move
                    ? timed_engine_move(original,p.wfh,time_stats_black,p.show_eval,chrono::duration<double>(p.time_limit_seconds),p.W_black,p.table,p.pipe)
                    : engine_move(original,p.wfh,p.show_eval,p.depth,p.W_black,p.table,p.pipe);
            }
            auto end_time = chrono::high_resolution_clock::now();
            auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
            
            if(p.is_supposed_to_give_out_move)
            cout << game_lenght  << ". "<< get_move(&store_original,original) << endl;

            if(!p.is_pretty_print&&p.print_Board)
            print(original->Board);
            else if(p.print_Board)
            write_to_python_script(p.pipe,get_UCI(&store_original,original));
            if(!p.is_human_play || p.colour!=WM)
            {
                cout << "Time taken for this move by the engine: " << duration << " ms" << endl;
            }
            //print infor about the lookup table
            if(p.table)
            {
                p.table->get_number_of_entrys();
                int number_of_full_collosion = p.table->get_number_of_full_collisions();
                bool there_are_doubles = p.table->there_are_doubles();
                cout << "Number of full collisions in the lookup table: " << number_of_full_collosion << endl;
                if(there_are_doubles)
                cout << "There are doubles in the lookup table!" << endl;
                else
                cout << "There are no doubles in the lookup table!" << endl;
                p.table->print_readout_delta(tt_insertions_before, tt_succ_before, tt_attempted_before);
                p.table->print_depth_bound_type_histogram();
            }
            
            if(take_history)
            history.push_back(*original);
            game_result=result(original);
            if(game_result)
            {
                print(original->Board);
                cout << endl << get_FEN(*original) << endl;
                break;
            
            }
            
        }

        
        if(p.table)
        {
            p.table->there_are_doubles();
        }
        if(p.is_supposed_to_print_PGN)
        cout << "\nPGN\n" << get_PGN(history,get_FEN(*p.original)) << endl;
        if(p.is_pretty_print)
        close_python_script(p.pipe);
        if(game_result==1)
        {
            cout << "White wins!" << endl;
            return 1;
        }
        if(game_result==-1)
        {
            cout << "Black wins!" << endl;
            return -1;
        }
        if(game_result==2)
        {
            cout << "Stalemate!" << endl;
            return 2;
        }
        return 3;

    }

    int improve_weighs(WEIGHTS* Weight_to_play_against=0)//max_game_lenght is there beause we currently cannot detect daws by repetition
{
    int counter =0;
    bool OG_plays_white=1;
    int result;
    p.is_supposed_to_give_out_move=0;
    p.is_supposed_to_print_PGN=0;

    for(int i=0;i<p.Number_of_games;i++)
    {
        history.clear();
        cout << "\nGame: " << i << endl;
        
        OG_plays_white=!OG_plays_white;
        
        WEIGHTS W_original;
        WEIGHTS W_contender;
        if(!Weight_to_play_against)
        {
            W_original.read_values_from_file(p.file_original);
            W_contender=W_original;
            W_contender.change_values(p.learning_rate,!OG_plays_white,p.probability_of_change);
        }
        else
        {
            W_original=*Weight_to_play_against;
            W_contender.read_values_from_file(p.file_original);
            W_contender.change_values(p.learning_rate,!OG_plays_white,p.probability_of_change);

        }
        WEIGHTS W_white,W_black;
        if(p.print_Board)
        {
            if(OG_plays_white)
            cout << "Black ist the contender!" << endl;
            else
            cout << "White ist the contender!" << endl;
        }
        
        if(OG_plays_white)
        {
            p.W_white=W_original;
            p.W_black=W_contender;
        }
        else
        {
            p.W_white=W_contender;
            p.W_black=W_original;
        }
        p.is_human_play=0;
        
        result=nicely_written_play();

        

        if(result==3)
        my_tournament.terminated_games++;
        if(result==2)
        my_tournament.draws++;
        if(result==1)
        my_tournament.white_wins++;
        if(result==-1)
        my_tournament.black_wins++;
        if(result==0)
        cout << "Error: no result" << endl;//this should be impossible anyway, but better safe than sorry

        if(OG_plays_white)
        if(result==-1)
        {
            cout << "Contender wins as black!" << endl;
            W_contender.print_values_to_file(p.file_original);
            W_contender.append_weights_to_File();
            counter++;
        }

        if(!OG_plays_white)
        if(result==1)
        {
            cout << "Contender wins as white!" << endl;
            W_contender.print_values_to_file(p.file_original);
            W_contender.append_weights_to_File();
            counter++;
        }

    }
    
    return counter;
}
    
};

class PRESENT
{
    private:
    
    PP p;
    BB original;
    BB* wfh;
    void welcome_message()
    {
        cout << "Welcome to Ascaniusfish!\n\n";
        top://my first goto statment, just for fun
        cout << "Do you want to play (p), watch (w) or read more about the project (r)?\n";
        char choice=' ';
        while(choice!='p' && choice!='r' && choice!='w')
        cin >> choice;
        if(choice=='p')
        p.is_human_play=1;
        else
        p.is_human_play=0;
        switch (choice)
        {
        case 'r':
        {
            cout << "Ascaniusfish is (currently) a determinitic chess engine-meaning the same conditions result always in the same outcome.\n";
            cout << "It uses a depth first search algorithm with alpha-beta pruning and a will soon incorporate quiescence search.\n";
            cout << "All feedback is appreciated, and thanks for playing!\n\n";
            cout << "The algorytm works best, if you choose an even depth(you may try to figure out why). The seach time grows exponentially with the depth, with (a roughly estimated) base 10-15.\n";
            cout << "Depth 2 will result in an basically intantanious move, while depth 4 will take a 2-3 seconds.\n";
            cout << "Depth 6 will take at times minutes, details will depend on your hardware, and on the progress of the game.\n";
            cout << "At depth 4 the engine has currenly a roughly approximatet elo of 1100, othe depths are insuficcently tested.\n";
            cout << "After the game you should close the python window, to get a pgn of the game.\n";
            cout << "You will figure out the rest on the fly, have fun!\n";
            goto top;
        }
        case 'p':
        {
            cout << "Do you want to play as white (w) or black (b)?\n";
            char col=' ';
            while(col!='w' && col!='b')
            cin >> col;
            if(col=='w')
            p.colour=1;
            else
            p.colour=0;
        } 
        case 'w':
        {
            cout << "Do you want to see the evaluation of the engine? (y/n)\n";
            char y=' ';
            while(y!='y' && y!='n')
            cin >> y;
            p.show_eval=y=='y';
            cout << "What depth do you want? (i reccomend 4, but if you are very impatient 2 is also ok)\n";
            cin >> p.depth;
            
            p.original=&original;
            wfh=new BB[80*(p.depth+max_non_king_pieces)]; // headroom for minimax_tactical()'s capture-only recursion, see max_non_king_pieces
            p.wfh=wfh;
            cout << "Do you want to start from a specific position (y/n)?\n";
            char y2=' ';
            cin >> y2;
            while(y2!='y' && y2!='n')
            cin >> y2;
            
            initialize_FEN_to::Standartboard(original.Board);
            if(y2=='y')
            {
            cout << "Please enter the FEN of the position you want to start from:\n";

            string FEN;
            cin.ignore(numeric_limits<std::streamsize>::max(), '\n');
            getline(cin,FEN);
            cout << "The FEN you entered is: " << FEN << endl;
            FEN_to_BB(FEN,&original);
            print(original.Board);
            }
             
        }
        }

    }

    public:
    void play()
    {
        welcome_message();
        
        p.max_game_lengh=150;
        Play play;
        p.table= new lookup_table;
        play.p=p;
        string continue_playing="y";
        do
        {
            play.nicely_written_play();
            p.table->get_number_of_entrys();
            cout << "The number of insertions: " << p.table->number_of_inserions << endl;
            cout << "The number of number_of_succ_readouts: " << p.table->number_of_succ_readouts << endl;
            cout << "THe total number of_readouts: " << p.table->number_of_attemted_readouts << endl;
            cout << "Do you want to play again? (y/n)\n";
            do{
                cin >> continue_playing;
            } while (continue_playing!="y" && continue_playing!="n");
        } while (continue_playing=="y");
        
        delete[] p.wfh;
        delete p.table;
        
        
    }
};

double round_to_percentage(double number)
{
    double return_value= round(number*100)/100;
    if(return_value<100 && return_value>99.99)
    return 100;
    return return_value;
}

double win_chance(int eval)
{
    return 50 + 50 * (2 / (1 + exp(-0.00368208 * eval)) - 1);
}

double accuracy(int eval_bevore, int eval_after)
{

    if(eval_after>eval_bevore)
    return 100;//if your move was better than the top move recommended by the engine you deserve at least 100% accuracy
    double return_value = 103.1668 * exp(-0.04354 * (win_chance(eval_bevore) - win_chance(eval_after))) - 3.1669;
    return max(0.0,round_to_percentage(return_value));
}

double* evaluate_game(vector<BB> history, const int depth, WEIGHTS W=WEIGHTS_OG)
{
    double accuracy_per_move[history.size()-1];
    double score=0;
    double eval_bevore[history.size()-1];
    double eval_after[history.size()-1];
    BB* wfh = new BB[300];
    for(int i=0;i<int(history.size())-1;i++)
    {   
        int corr_f=1-2*!history[i].white_move;
        
        eval_bevore[i]=minimax(&history[i],wfh,depth,W).eval*corr_f;
        auto temp = all_moves(&history[i],wfh);

        int number_of_new_moves = std::get<0>(temp);
        if(depth==0)
        {
            cout << "Error: The game is not over" << endl;
            exit(1);
        }
        
        int index_of_alignment=-1;
        for(int j=0;j<number_of_new_moves;j++)
        {
            if(are_equal(&history[i+1],&wfh[j]))
            {
                index_of_alignment=j;
                break;
            }
        }
        if(index_of_alignment==-1)
        {
            cout << "Error: The move was not found" << endl;
            exit(1);
        }
        
        
        int depth_to_use=depth;
        if(captures_more_valuable_piece(&history[i],&wfh[index_of_alignment],W))
        depth_to_use++;
        eval_after[i]=minimax(&history[i+1],wfh,depth_to_use,W).eval*corr_f;
        
        if(eval_after[i]<INT_MIN+max_mating_seq)
        eval_after[i]++;
        if(eval_after[i]>INT_MAX-max_mating_seq)
        eval_after[i]--;
        
        
        
        accuracy_per_move[i]=accuracy(eval_bevore[i],eval_after[i]);
        print(history[i].Board);
        cout << "The move was: " << get_move(&history[i],&history[i+1]) << " and the accuracy was: " << accuracy_per_move[i] << "%" << endl;
        
        
       
    }
    int white_moves=0;
    for(int i=0;i<int(history.size())-1;i=i+2)  
    {
        score+=accuracy_per_move[i];
        white_moves++;
    } 
    double* return_value = new double[2];
    return_value[0]=round_to_percentage(score/white_moves);
    int black_moves=0;
    for(int i=1;i<int(history.size())-1;i=i+2)  
    {
        score+=accuracy_per_move[i];
        black_moves++;
    }
    return_value[1]=round_to_percentage(score/(white_moves+black_moves));

    
    return return_value;
}

#endif
