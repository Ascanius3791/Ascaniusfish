// OWNERSHIP=Claude
// Sweeps LOOKUP_TABLE_LAMBDA (see LOOKUP_TABLE_LAMBDA guard in
// src/lookup_table.cpp's value_for_victim_index()) by being recompiled once per
// value with -DLOOKUP_TABLE_LAMBDA=<value> (see run_lambda_sweep.sh). Self-plays
// from the starting position with a persistent lookup table at a fixed search
// depth: a silent warm-up phase fills the table close to capacity first (lambda
// only matters once eviction, not "first free slot", is what find_victim_index()
// actually decides - see run_lambda_sweep.sh's comment for how warmup_plies was
// picked), then a measured phase reports per-ply and total node counts / wall
// time / TT insertion+readout stats so the lambda values can be compared.
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>

using clock_type = std::chrono::steady_clock;

static void initialize_starting_position(BB* position)
{
    FEN_to_BB(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        position);
    castling_rights(position);
}

// Mirrors Play::engine_move()'s body (iterative deepening 1..depth feeding the
// same persistent table, then matching pv_line.moves[0] back to a wfh index) -
// duplicated here rather than calling the class method, since Play also drags
// in python-pipe/opening-book setup this benchmark doesn't need.
static int engine_move(BB* original, BB* wfh, int depth, lookup_table* table)
{
    auto generated = all_moves(original, wfh);
    const int number_of_new_moves = std::get<0>(generated);
    const std::vector<Move> moves = std::get<1>(generated);
    if(number_of_new_moves == 0)
        return 0;
    if(number_of_new_moves == 1)
    {
        copy_BB(wfh, original);
        return INT_MAX / 2;
    }

    PV_Line pv_line;
    for(int d = 1; d <= depth; ++d)
        pv_line = minimax(original, wfh + number_of_new_moves, d, WEIGHTS_OG,
                          INT_MIN, INT_MAX, table);

    int best_move_index = 0;
    for(int i = 0; i < number_of_new_moves; ++i)
    {
        if(moves[i] == pv_line.moves[0])
        {
            best_move_index = i;
            break;
        }
    }

    copy_BB(wfh + best_move_index, original);
    return pv_line.eval;
}

int main(int argc, char** argv)
{
    const int counted_plies = argc > 1 ? std::atoi(argv[1]) : 10;
    const int depth = argc > 2 ? std::atoi(argv[2]) : 5;
    // Calibrated separately (see run_lambda_sweep.sh): at depth 5 from the start
    // position, the TT_EXPONENT_FOR_SIZE=15/TT_BUCKET_SIZE=8 table (262144 slots)
    // has every bucket touched and sits at ~95% average fill by ply 18.
    const int warmup_plies = argc > 3 ? std::atoi(argv[3]) : 18;

    // Must run before anything touches a BB's zobrist_hash (see main() in
    // ascaniusfish.cpp) - without this, Zobrist's static keys are all
    // zero-initialized, every position hashes to 0, and the whole table
    // degenerates into a single bucket. (Found the hard way: an earlier version
    // of this benchmark omitted this and every lambda value looked identical.)
    Zobrist zobrist_init = Zobrist();
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB position;
    initialize_starting_position(&position);
    BB* workspace = new BB[200000];
    lookup_table* table = new lookup_table;

    std::cout << "lambda=" << LOOKUP_TABLE_LAMBDA
              << " warmup_plies=" << warmup_plies
              << " counted_plies=" << counted_plies << " depth=" << depth << std::endl;

    for(int ply = 1; ply <= warmup_plies && result(&position) == 0; ++ply)
        engine_move(&position, workspace, depth, table);

    std::cout << "after warmup: ";
    table->get_number_of_entrys();

    long long total_calls_before_run = number_of_mimimax_calls;
    const clock_type::time_point run_start = clock_type::now();

    for(int ply = 1; ply <= counted_plies && result(&position) == 0; ++ply)
    {
        const int calls_before = number_of_mimimax_calls;
        const int insertions_before = table->number_of_inserions;
        const int succ_before = table->number_of_succ_readouts;
        const int attempted_before = table->number_of_attemted_readouts;
        const clock_type::time_point ply_start = clock_type::now();

        const int eval = engine_move(&position, workspace, depth, table);

        const clock_type::time_point ply_stop = clock_type::now();
        const long double ply_ms =
            std::chrono::duration<long double, std::milli>(ply_stop - ply_start).count();
        const int calls_delta = number_of_mimimax_calls - calls_before;

        std::cout << "  ply " << ply
                  << " | nodes=" << calls_delta
                  << " | ms=" << std::fixed << std::setprecision(2) << static_cast<double>(ply_ms)
                  << " | knps=" << std::setprecision(1)
                  << (ply_ms > 0 ? static_cast<double>(calls_delta) / static_cast<double>(ply_ms) : 0.0)
                  << " | eval=" << eval << std::endl;
        table->print_readout_delta(insertions_before, succ_before, attempted_before);
    }

    const clock_type::time_point run_stop = clock_type::now();
    const long double total_ms =
        std::chrono::duration<long double, std::milli>(run_stop - run_start).count();
    const long long total_calls = number_of_mimimax_calls - total_calls_before_run;

    std::cout << "TOTAL lambda=" << LOOKUP_TABLE_LAMBDA
              << " nodes=" << total_calls
              << " ms=" << std::fixed << std::setprecision(2) << static_cast<double>(total_ms)
              << " knps=" << std::setprecision(1)
              << (total_ms > 0 ? static_cast<double>(total_calls) / static_cast<double>(total_ms) : 0.0)
              << " full_collisions=" << table->get_number_of_full_collisions()
              << std::endl;
    std::cout << "after measured phase: ";
    table->get_number_of_entrys();

    delete table;
    delete[] workspace;
    return 0;
}
