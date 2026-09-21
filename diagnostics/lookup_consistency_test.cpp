// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

static void expect(bool condition, const std::string& message)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

static std::string move_name(const BB* position, const Move& move)
{
    BB* moves = new BB[256];
    auto generated = all_moves(position, moves, 256);
    const int number_of_moves = std::get<0>(generated);
    const std::vector<Move>& legal_moves = std::get<1>(generated);
    for(int index=0; index<number_of_moves; ++index)
    {
        if(legal_moves[index] == move)
        {
            std::string result = get_move(position, moves + index);
            delete[] moves;
            return result;
        }
    }
    delete[] moves;
    return "<not-found>";
}

static int matching_legal_move(const BB* position, const Move& target, BB* boards)
{
    auto generated = all_moves(position, boards, 256);
    const int number_of_moves = std::get<0>(generated);
    const std::vector<Move>& moves = std::get<1>(generated);
    int matches = 0;

    for(int index=0; index<number_of_moves; ++index)
    {
        if(moves[index] == target)
            ++matches;
    }
    return matches;
}

static void test_cached_engine_move(const BB& position, int depth)
{
    BB uncached_position = position;
    BB warmed_position = position;
    BB cached_position = position;
    BB* workspace = new BB[200000];
    lookup_table* table = new lookup_table;
    Play engine;

    const int uncached_eval = engine.engine_move(
        &uncached_position, workspace, false, depth, WEIGHTS_OG, nullptr);

    engine.engine_move(&warmed_position, workspace, false, depth, WEIGHTS_OG, table);

    const int cached_eval = engine.engine_move(
        &cached_position, workspace, false, depth, WEIGHTS_OG, table);

    const std::string uncached_move = get_move(&position, &uncached_position);
    const std::string cached_move = get_move(&position, &cached_position);
    expect(uncached_move == cached_move,
           "lookup table changed engine_move from " + uncached_move +
           " to " + cached_move);
    expect(uncached_eval == cached_eval,
           "lookup table changed engine_move evaluation");
    expect(are_equal(&uncached_position, &cached_position),
           "cached and uncached engine_move produced different boards");

    TT_readout root_entry = table->is_retrivable_eval(&position, depth);
    expect(root_entry.is_found, "warmed root position was not stored");
    expect(root_entry.pv_line.bound_type == 0, "warmed root position is not exact");
    expect(root_entry.pv_line.current_lenght > 0, "warmed root PV has no move");

    BB* legal_boards = new BB[256];
    const int matches = matching_legal_move(
        &position, root_entry.pv_line.moves[0], legal_boards);
    expect(matches == 1,
           "cached PV first move matched " + std::to_string(matches) +
           " legal moves instead of exactly one");
    expect(move_name(&position, root_entry.pv_line.moves[0]) == cached_move,
           "cached PV first move does not produce the selected board");

    delete[] legal_boards;
    delete table;
    delete[] workspace;
}

int main()
{
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB position;
    FEN_to_BB("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &position);
    castling_rights(&position);

    test_cached_engine_move(position, 3);
    std::cout << "lookup_consistency_test passed" << std::endl;
    return 0;
}
