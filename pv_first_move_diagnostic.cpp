// OWNERSHIP=Claude
#include "ascaniusfish.hpp"
#include "ascaniusfish_2.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

static int move_index(const BB* position, const Move& target, BB* boards)
{
    auto generated = all_moves(position, boards, 256);
    const int number_of_moves = std::get<0>(generated);
    const std::vector<Move>& moves = std::get<1>(generated);
    for(int index=0; index<number_of_moves; ++index)
    {
        if(moves[index] == target)
        return index;
    }
    return -1;
}

static void initialize_starting_position(BB* position)
{
    initialize_FEN_to::Standartboard(position->Board);
    position->white_move=1;
    position->en_passant=0;
    position->move=1;
    position->halfmoves_since_last_capture_or_pawn_move=0;
    castling_rights(position);
}

int main(int argc, char** argv)
{
    int max_halfmoves=24;
    int depth=3;
    if(argc>1)
    max_halfmoves=std::atoi(argv[1]);
    if(argc>2)
    depth=std::atoi(argv[2]);
    if(max_halfmoves<1)
    max_halfmoves=1;
    if(depth<1)
    depth=1;

    initialize_rand();
    // Populates the static Zobrist key tables. Without this every key is 0, so every
    // position hashes to 0, the transposition table collapses into one bucket and any
    // probe matches the first entry stored - one arbitrary position's result is handed
    // out for all of them. Matching is by hash alone now, so omitting this silently
    // corrupts results instead of merely slowing things down.
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    lookup_table* table = new lookup_table;
    Play engine;
    BB* workspace = new BB[200000];
    BB position;
    initialize_starting_position(&position);

    // First pass populates the table, recording the exact position reached
    // before each move. History-heuristic move ordering (see butterfly
    // history in ascaniusfish_2.hpp) carries state across searches, so a
    // second replay of the game from scratch is not guaranteed to retrace
    // the same trajectory - probing the table against these saved snapshots
    // instead of a re-simulated game keeps this diagnostic correct regardless.
    std::vector<BB> position_history;
    for(int ply=0; ply<max_halfmoves && result(&position)==0; ++ply)
    {
        position_history.push_back(position);
        engine.engine_move(&position, workspace, false, depth, WEIGHTS_OG, table);
    }

    int eligible=0;
    int matches=0;
    int best_by_eval=0;
    int mismatches=0;
    int exact_entries=0;
    for(size_t history_index=0; history_index<position_history.size(); ++history_index)
    {
        int ply = static_cast<int>(history_index);
        BB position = position_history[history_index];
        TT_readout readout = table->is_retrivable_eval(&position, depth);
        if(readout.is_found && readout.pv_line.current_lenght>0)
        {
            ++eligible;
            if(readout.pv_line.bound_type==0)
            ++exact_entries;
            const Move pv_move = readout.pv_line.moves[0];
            BB uncached_position=position;
            const int best_eval = engine.engine_move(
                &uncached_position, workspace, false, depth, WEIGHTS_OG,
                nullptr);
            BB* legal_boards = new BB[256];
            const int pv_index = move_index(&position, pv_move, legal_boards);
            const bool pv_is_legal = pv_index >= 0;
            bool is_match = false;
            bool is_best_by_eval = false;
            if(pv_is_legal)
            {
            is_match = are_equal(&legal_boards[pv_index], &uncached_position);
            // engine_move() searches the root at `depth`, so it gives this child
            // depth-1. Searching it at `depth` compared scores from two different
            // depths and read ~0% no matter how healthy the engine was.
            int pv_depth=depth-1;
            if(captures_more_valuable_piece(&position, legal_boards+pv_index,
                                            WEIGHTS_OG))
            ++pv_depth;
            // wfh must be scratch memory disjoint from `original` (legal_boards+pv_index) -
            // passing legal_boards itself here aliased the PV candidate's own board with
            // the recursion's write buffer and corrupted the heap once this code path
            // actually ran (previously masked entirely by the eligible==0 bug above).
            PV_Line pv_result = minimax(legal_boards+pv_index, workspace,
                                         pv_depth, WEIGHTS_OG, INT_MIN,
                                         INT_MAX, nullptr);
            int pv_eval=pv_result.eval;
            if(pv_eval<INT_MIN+max_mating_seq)
            ++pv_eval;
            if(pv_eval>INT_MAX-max_mating_seq)
            --pv_eval;
            is_best_by_eval = position.white_move ? pv_eval>=best_eval
                                                   : pv_eval<=best_eval;
            }
            if(is_match)
            ++matches;
            else
            ++mismatches;
            if(is_best_by_eval)
            ++best_by_eval;
            std::cout << "ply " << ply+1 << ": PV "
                      << (pv_is_legal ? "legal" : "INVALID")
                      << (is_match ? ", same move" : ", different move")
                      << (is_best_by_eval ? ", best by eval" : ", not best by eval")
                      << std::endl;
            delete[] legal_boards;
        }
    }

    const double percentage = eligible==0 ? 0.0 : 100.0*matches/eligible;
     std::cout << "PV first-move diagnostic: " << matches << "/" << eligible
                  << " exact PVs selected the same move as the independently "
                      "searched best move ("
              << std::fixed << std::setprecision(2) << percentage << "%)."
              << std::endl;
     const double best_percentage = eligible==0 ? 0.0 :
                                              100.0*best_by_eval/eligible;
     std::cout << "PV first-move by evaluation: " << best_by_eval << "/"
                  << eligible << " were tied for the best fresh evaluation ("
                  << std::fixed << std::setprecision(2) << best_percentage << "%)."
                  << std::endl;
    std::cout << "Entries marked exact by the table: " << exact_entries << "/"
              << eligible << std::endl;
    std::cout << "Mismatches: " << mismatches << std::endl;
    delete[] workspace;
    delete table;
    return eligible==0 ? 1 : 0;
}
