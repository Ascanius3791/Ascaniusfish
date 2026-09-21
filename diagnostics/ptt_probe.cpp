// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

// lookup_table/PTT objects are hundreds of MB to low-GB (TT_entry is ~2.2KB,
// dominated by PV_Line's fixed Move[MAX_PV_Lenght] array, times bucket_size times
// table size) - always heap-allocate them (never as a local/stack variable, which
// would overflow the stack immediately) and delete promptly so at most one such
// object is alive at a time, the same way the real engine only ever uses `new
// lookup_table` (see ascaniusfish.cpp).

static void expect(bool condition, const std::string& message)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

// Builds a TT_entry with a synthetic (not real-Zobrist) hash so tests can force
// specific bucket placement, and a distinguishing en_passant value so are_equal()
// treats each one as a different position (are_equal compares Board/castle/
// en_passant/white_move, not the TT_entry-level zobrist_hash or move counters).
static TT_entry make_entry(uint64_t hash, uint64_t distinguishing_en_passant, int depth, int bound_type=0)
{
    TT_entry entry;
    entry.initialized = true;
    entry.board = BB();
    entry.board.en_passant = distinguishing_en_passant;
    entry.zobrist_hash = hash;
    entry.pv_line = PV_Line();
    entry.pv_line.depth = depth;
    entry.pv_line.eval = depth * 10;
    entry.pv_line.bound_type = bound_type;
    entry.is_from_opening_book = false;
    return entry;
}

static BB lookup_key_for(const TT_entry& entry)
{
    BB key = entry.board;
    key.zobrist_hash = entry.zobrist_hash;
    return key;
}

static void test_base_lookup_table_unchanged()
{
    lookup_table* table = new lookup_table(); // lookup_table_base<15,8> alias
    TT_entry entry = make_entry(/*hash=*/111, /*en_passant=*/1, /*depth=*/4);
    table->insert(entry);

    BB key = lookup_key_for(entry);
    TT_readout readout = table->is_retrivable_eval(&key, 0);
    expect(readout.is_found, "base lookup_table: inserted entry should be found");
    expect(readout.pv_line.depth==4, "base lookup_table: retrieved depth should match inserted depth");
    delete table;
}

static void test_round_trip(const std::string& path)
{
    TT_entry a = make_entry(/*hash=*/1, /*en_passant=*/1, /*depth=*/6);
    TT_entry b = make_entry(/*hash=*/2, /*en_passant=*/2, /*depth=*/9);
    {
        PTT* ptt = new PTT();
        ptt->insert(a);
        ptt->insert(b);
        expect(ptt->save(path), "round-trip: save() should succeed");
        delete ptt;
    }
    {
        PTT* loaded = new PTT();
        expect(loaded->load(path), "round-trip: load() should succeed and read back every saved entry");

        BB key_a = lookup_key_for(a);
        BB key_b = lookup_key_for(b);
        TT_readout readout_a = loaded->is_retrivable_eval(&key_a, 0);
        TT_readout readout_b = loaded->is_retrivable_eval(&key_b, 0);
        expect(readout_a.is_found && readout_a.pv_line.depth==6, "round-trip: entry a should round-trip with its depth");
        expect(readout_b.is_found && readout_b.pv_line.depth==9, "round-trip: entry b should round-trip with its depth");
        delete loaded;
    }
}

static void test_monotonic_load(const std::string& path)
{
    TT_entry shallow = make_entry(/*hash=*/5, /*en_passant=*/5, /*depth=*/3);
    TT_entry deep = make_entry(/*hash=*/5, /*en_passant=*/5, /*depth=*/8);
    BB key = lookup_key_for(deep); // same board identity as shallow, only depth differs

    // File on disk holds a shallow entry (depth 3).
    {
        PTT* shallow_source = new PTT();
        shallow_source->insert(shallow);
        expect(shallow_source->save(path), "monotonic: saving shallow entry should succeed");
        delete shallow_source;
    }

    // A PTT that already has a DEEPER in-memory entry for the same position must
    // keep it after loading the shallower file - insert() enforces this already.
    {
        PTT* deeper_in_memory = new PTT();
        deeper_in_memory->insert(deep);
        deeper_in_memory->load(path);
        TT_readout readout = deeper_in_memory->is_retrivable_eval(&key, 0);
        expect(readout.is_found && readout.pv_line.depth==8, "monotonic: loading a shallower entry must not clobber a deeper in-memory one");
        delete deeper_in_memory;
    }

    // File on disk now holds the deep entry (depth 8).
    {
        PTT* deep_source = new PTT();
        deep_source->insert(deep);
        expect(deep_source->save(path), "monotonic: saving deep entry should succeed");
        delete deep_source;
    }

    // A PTT with only a SHALLOWER in-memory entry must pick up the deeper one from disk.
    {
        TT_entry shallow2 = make_entry(/*hash=*/5, /*en_passant=*/5, /*depth=*/1);
        PTT* shallower_in_memory = new PTT();
        shallower_in_memory->insert(shallow2);
        shallower_in_memory->load(path);
        TT_readout readout = shallower_in_memory->is_retrivable_eval(&key, 0);
        expect(readout.is_found && readout.pv_line.depth==8, "monotonic: loading a deeper entry must replace a shallower in-memory one");
        delete shallower_in_memory;
    }
}

static void test_eviction_prefers_depth()
{
    // PTT's base is lookup_table_base<16, 8> - bucket_size is 8. Force 9 distinct
    // positions into the SAME bucket by giving them all the same TT_entry-level
    // hash (insert()/find_victim_index() route by that field, not by the board's
    // own zobrist hash), with strictly increasing depth. The 9th insert should
    // evict the depth-1 entry (lowest value under PTT's depth-only scoring),
    // not whichever the base TT's move-number-weighted policy would pick.
    PTT* ptt = new PTT();
    const uint64_t shared_hash = 777;
    TT_entry depth1_entry = make_entry(shared_hash, /*en_passant=*/1, /*depth=*/1);
    for(int depth=1; depth<=8; depth++)
    {
        ptt->insert(make_entry(shared_hash, /*en_passant=*/depth, depth));
    }

    TT_entry newcomer = make_entry(shared_hash, /*en_passant=*/9, /*depth=*/5);
    ptt->insert(newcomer);

    BB key_depth1 = lookup_key_for(depth1_entry);
    BB key_newcomer = lookup_key_for(newcomer);

    TT_readout readout_depth1 = ptt->is_retrivable_eval(&key_depth1, 0);
    TT_readout readout_newcomer = ptt->is_retrivable_eval(&key_newcomer, 0);
    expect(!readout_depth1.is_found, "eviction: the lowest-depth entry in a full bucket should have been evicted");
    expect(readout_newcomer.is_found, "eviction: the depth-5 newcomer should have displaced the depth-1 entry");
    delete ptt;
}

int main()
{
    std::string path = "/tmp/ptt_probe_test.bin";

    test_base_lookup_table_unchanged();
    test_round_trip(path);
    test_monotonic_load(path);
    test_eviction_prefers_depth();

    std::remove(path.c_str());
    std::cout << "All PTT probe checks passed." << std::endl;
    return 0;
}
