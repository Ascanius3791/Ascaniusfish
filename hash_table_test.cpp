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

    // Stored the way minimax() itself stores a resolved node: an exact PV_Line
    // wrapped in a TT_entry keyed by the board's own zobrist hash.
    PV_Line pv_line(123);
    pv_line.depth=3;
    pv_line.bound_type=0; // exact
    pv_line.current_lenght=0;

    TT_entry entry;
    entry.zobrist_hash=position.zobrist_hash;
    entry.pv_line=pv_line;
    entry.initialized=true;
    table->insert(entry);

    TT_readout same_readout=table->is_retrivable_eval(&position,3);
    expect(same_readout.is_found,"same position should be retrievable");
    expect(same_readout.pv_line.eval==123,"retrieved eval should match inserted eval");
    expect(same_readout.pv_line.depth==3,"retrieved depth should match stored depth");

    // is_retrivable_eval() itself doesn't filter by requested depth anymore -
    // that decision (depth<=readout.pv_line.depth) now lives in minimax()'s
    // caller-side check. So a "too deep" request still finds the entry, but
    // callers can see from pv_line.depth that it isn't deep enough to trust.
    TT_readout too_deep_readout=table->is_retrivable_eval(&position,4);
    expect(too_deep_readout.is_found,"entry is still found for a deeper request");
    expect(too_deep_readout.pv_line.depth<4,"stored depth should be below the requested depth");

    // TT_entry no longer stores the full board, only its zobrist hash, so the
    // table can no longer distinguish two positions that happen to collide on
    // that hash - it trusts the hash completely. These two cases used to be
    // caught by a full-board equality check on read; that safety net is gone
    // (a real Zobrist difference, like the one below, still works fine).
    BB side_to_move_changed=position;
    side_to_move_changed.white_move=!side_to_move_changed.white_move;
    side_to_move_changed.zobrist_hash=Zobrist::compute_Zobrist_Hash(side_to_move_changed);
    expect(!table->is_retrivable_eval(&side_to_move_changed,3).is_found,"a position with a genuinely different zobrist hash should not be retrievable");
}

static void test_minimax_lookup(lookup_table* table)
{
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB position;
    FEN_to_BB("4k3/8/8/8/8/8/8/4K3 w - - 0 1",&position);
    BB* wfh = new BB[512];

    const int eval_1=minimax(&position,wfh,2,WEIGHTS_OG,INT_MIN,INT_MAX,table).eval;
    const int successful_readouts_before_second_run=table->number_of_succ_readouts;
    const int eval_2=minimax(&position,wfh,2,WEIGHTS_OG,INT_MIN,INT_MAX,table).eval;

    delete[] wfh;

    expect(eval_1==eval_2,"cached minimax eval should match original minimax eval");
    expect(table->number_of_succ_readouts>successful_readouts_before_second_run,"second minimax run should hit the table");
}

int main()
{
    // TT_entry now identifies a position purely by its zobrist hash (no more
    // full-board fallback), so the Zobrist random tables need to be up before
    // any test that inserts/looks up an entry - previously test_direct_lookup
    // didn't need real hashes since it relied on full-board equality.
    Zobrist zobrist_keys;

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
