// OWNERSHIP=Claude
#include "ascaniusfish.hpp"
#include "ascaniusfish_2.hpp"

#include <cstdlib>
#include <iostream>

static void expect(bool condition, const char* message)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

static void test_direct_lookup(lookup_table* table)
{
    BB position;
    FEN_to_BB("4k3/8/8/8/8/8/8/4K3 w - - 0 1",&position);
    position.eval=123;
    position.is_evaluated=1;
    position.depth_of_eval=3;

    table->insert(position,3);

    BB same_position=position;
    same_position.eval=0;
    same_position.is_evaluated=0;
    same_position.depth_of_eval=0;
    expect(table->is_retrivable_eval(&same_position,3),"same position should be retrievable at stored depth");
    expect(same_position.eval==123,"retrieved eval should match inserted eval");

    BB too_deep=position;
    too_deep.eval=0;
    expect(!table->is_retrivable_eval(&too_deep,4),"position should not be retrievable above stored depth");

    BB side_to_move_changed=position;
    side_to_move_changed.white_move=!side_to_move_changed.white_move;
    expect(!table->is_retrivable_eval(&side_to_move_changed,3),"different side to move should not be retrievable");

    BB en_passant_changed=position;
    en_passant_changed.en_passant=1ULL << 16;
    expect(!table->is_retrivable_eval(&en_passant_changed,3),"different en passant state should not be retrievable");
}

static void test_minimax_lookup(lookup_table* table)
{
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB position;
    FEN_to_BB("4k3/8/8/8/8/8/8/4K3 w - - 0 1",&position);
    BB* wfh = new BB[512];

    const int eval_1=minimax(&position,wfh,2,0,WEIGHTS_OG,INT_MIN,INT_MAX,table);
    const int successful_readouts_before_second_run=table->number_of_succ_readouts;
    const int eval_2=minimax(&position,wfh,2,0,WEIGHTS_OG,INT_MIN,INT_MAX,table);

    delete[] wfh;

    expect(eval_1==eval_2,"cached minimax eval should match original minimax eval");
    expect(table->number_of_succ_readouts>successful_readouts_before_second_run,"second minimax run should hit the table");
}

int main()
{
    lookup_table* table = new lookup_table;

    test_direct_lookup(table);
    table->reset();
    test_minimax_lookup(table);

    std::cout << "hash_table_test passed" << std::endl;
    std::cout << "insertions: " << table->number_of_inserions << std::endl;
    std::cout << "attempted readouts: " << table->number_of_attemted_readouts << std::endl;
    std::cout << "successful readouts: " << table->number_of_succ_readouts << std::endl;

    delete table;
    return 0;
}
