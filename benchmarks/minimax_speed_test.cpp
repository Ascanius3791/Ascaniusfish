// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>

using clock_type = std::chrono::steady_clock;

struct timing_stats
{
    long long calls = 0;
    long double total_microseconds = 0;

    void add(clock_type::time_point start, clock_type::time_point stop)
    {
        ++calls;
        total_microseconds +=
            std::chrono::duration<long double, std::micro>(stop - start).count();
    }

    long double average_microseconds() const
    {
        return calls == 0 ? 0 : total_microseconds / calls;
    }
};

struct benchmark_stats
{
    timing_stats without_lookup;
    timing_stats persistent_lookup;
    timing_stats fresh_lookup;
    timing_stats fresh_iterative_deepening;
    long long exact_comparisons = 0;
    long long exact_variations = 0;
    long long non_exact_comparisons = 0;
    long long non_exact_variations = 0;
};

static void compare_results(
    const PV_Line& reference,
    const PV_Line& candidate,
    benchmark_stats* stats)
{
    const bool both_exact = reference.bound_type == 0 && candidate.bound_type == 0;
    if(both_exact)
    {
        ++stats->exact_comparisons;
        if(reference.eval != candidate.eval)
            ++stats->exact_variations;
    }
    else
    {
        ++stats->non_exact_comparisons;
        if(reference.eval != candidate.eval)
            ++stats->non_exact_variations;
    }
}

static PV_Line timed_minimax(
    const BB* original,
    BB* workspace,
    int depth,
    WEIGHTS weights,
    int alpha,
    int beta,
    lookup_table* table,
    timing_stats* stats)
{
    const clock_type::time_point start = clock_type::now();
    PV_Line line = minimax(original, workspace, depth, weights, alpha, beta, table);
    const clock_type::time_point stop = clock_type::now();
    stats->add(start, stop);
    return line;
}

static PV_Line timed_iterative_deepening(
    const BB* original,
    BB* workspace,
    int max_depth,
    WEIGHTS weights,
    int alpha,
    int beta,
    lookup_table* table,
    timing_stats* stats)
{
    const clock_type::time_point start = clock_type::now();
    PV_Line line;
    for(int current_depth = 0; current_depth <= max_depth; ++current_depth)
        line = minimax(original, workspace, current_depth, weights,
                       alpha, beta, table);
    const clock_type::time_point stop = clock_type::now();
    stats->add(start, stop);
    return line;
}

static int benchmark_engine_move(
    BB* original,
    BB* workspace,
    int depth,
    WEIGHTS weights,
    lookup_table* persistent_table,
    lookup_table* fresh_table,
    lookup_table* fresh_iterative_table,
    benchmark_stats* stats)
{
    int alpha = INT_MIN;
    int beta = INT_MAX;
    int best_move_index = 0;

    auto generated = all_moves(original, workspace);
    const int number_of_moves = std::get<0>(generated);
    const std::vector<Move> moves = std::get<1>(generated);
    if(number_of_moves == 0)
        return 0;
    if(number_of_moves == 1)
    {
        copy_BB(workspace, original);
        return INT_MAX / 2;
    }

    PV_Line tt_hint;
    TT_readout readout = persistent_table->is_retrivable_eval(original, depth);
    if(readout.is_found)
        tt_hint = readout.pv_line;

    const std::vector<int> indices = sorting_moves(
        workspace, moves, number_of_moves, original->white_move,
        &tt_hint, 0, weights);

    PV_Line best_line(original->white_move ? INT_MIN : INT_MAX);
    bool best_line_initialized = false;

    for(int move_number = 0; move_number < number_of_moves; ++move_number)
    {
        const int workspace_index = indices[move_number];
        const int search_depth = depth - 1 +
            (captures_more_valuable_piece(
                original, workspace + workspace_index, weights) ? 1 : 0);

        const PV_Line without_lookup = timed_minimax(
            workspace + workspace_index, workspace + number_of_moves,
            search_depth, weights, alpha, beta, nullptr,
            &stats->without_lookup);
        const PV_Line persistent_lookup = timed_minimax(
            workspace + workspace_index, workspace + number_of_moves,
            search_depth, weights, alpha, beta, persistent_table,
            &stats->persistent_lookup);

        fresh_table->reset();
        const PV_Line fresh_lookup = timed_minimax(
            workspace + workspace_index, workspace + number_of_moves,
            search_depth, weights, alpha, beta, fresh_table,
            &stats->fresh_lookup);

        fresh_iterative_table->reset();
        const PV_Line fresh_iterative_deepening = timed_iterative_deepening(
            workspace + workspace_index, workspace + number_of_moves,
            search_depth, weights, alpha, beta, fresh_iterative_table,
            &stats->fresh_iterative_deepening);

        compare_results(without_lookup, persistent_lookup, stats);
        compare_results(without_lookup, fresh_lookup, stats);
        compare_results(without_lookup, fresh_iterative_deepening, stats);

        const int eval = persistent_lookup.eval;
        if(!best_line_initialized ||
           (original->white_move ? eval > best_line.eval : eval < best_line.eval))
        {
            best_move_index = workspace_index;
            best_line = PV_Line(moves[workspace_index], depth, &persistent_lookup);
            best_line_initialized = true;
        }

        if(original->white_move)
        {
            best_line.eval = std::max(best_line.eval, eval);
            alpha = std::max(alpha, eval);
        }
        else
        {
            best_line.eval = std::min(best_line.eval, eval);
            beta = std::min(beta, eval);
        }

        if(beta <= alpha)
            break;
    }

    copy_BB(workspace + best_move_index, original);
    return best_line.eval;
}

static void print_running_average(const benchmark_stats& stats, int ply)
{
    std::cout << "ply " << ply
              << " | average us: uncached=" << std::fixed << std::setprecision(1)
              << static_cast<double>(stats.without_lookup.average_microseconds())
              << ", persistent lookup="
              << static_cast<double>(stats.persistent_lookup.average_microseconds())
              << ", fresh lookup="
              << static_cast<double>(stats.fresh_lookup.average_microseconds())
              << ", fresh lookup + iterative deepening="
              << static_cast<double>(stats.fresh_iterative_deepening.average_microseconds())
              << " | minimax calls=" << stats.persistent_lookup.calls
              << " | exact variations=" << stats.exact_variations << "/"
              << stats.exact_comparisons
              << " | non-exact variations=" << stats.non_exact_variations << "/"
              << stats.non_exact_comparisons << std::endl;
}

static void initialize_starting_position(BB* position)
{
    FEN_to_BB(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        position);
    castling_rights(position);
}

int main(int argc, char** argv)
{
    int max_halfmoves = argc > 1 ? std::atoi(argv[1]) : 20;
    const int depth = argc > 2 ? std::atoi(argv[2]) : 3;
    if(max_halfmoves < 1)
        max_halfmoves = 1;

    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB position;
    initialize_starting_position(&position);
    BB* workspace = new BB[200000];
    lookup_table* persistent_table = new lookup_table;
    lookup_table* fresh_table = new lookup_table;
    lookup_table* fresh_iterative_table = new lookup_table;
    benchmark_stats stats;

    for(int ply = 1; ply <= max_halfmoves && result(&position) == 0; ++ply)
    {
        benchmark_engine_move(
            &position, workspace, depth, WEIGHTS_OG,
            persistent_table, fresh_table, fresh_iterative_table, &stats);
        print_running_average(stats, ply);
    }

    std::cout << "Final averages over " << stats.persistent_lookup.calls
              << " minimax calls:" << std::endl;
    std::cout << "  uncached: "
              << static_cast<double>(stats.without_lookup.average_microseconds())
              << " us" << std::endl;
    std::cout << "  persistent lookup: "
              << static_cast<double>(stats.persistent_lookup.average_microseconds())
              << " us" << std::endl;
    std::cout << "  fresh lookup: "
              << static_cast<double>(stats.fresh_lookup.average_microseconds())
              << " us" << std::endl;
    std::cout << "  fresh lookup + iterative deepening: "
              << static_cast<double>(stats.fresh_iterative_deepening.average_microseconds())
              << " us" << std::endl;
    std::cout << "Exact result variations: " << stats.exact_variations
              << " out of " << stats.exact_comparisons << " comparisons" << std::endl;
    std::cout << "Non-exact result variations: " << stats.non_exact_variations
              << " out of " << stats.non_exact_comparisons << " comparisons" << std::endl;

    delete persistent_table;
    delete fresh_table;
    delete fresh_iterative_table;
    delete[] workspace;
    return 0;
}
