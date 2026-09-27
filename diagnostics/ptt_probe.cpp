// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

// lookup_table/PTT objects are tens of MB (~46MB and ~92MB respectively: TT_entry
// is ~184 bytes since PV_Line keeps PV_CHUNK moves inline, times bucket_size times
// table size), which is still far past the default ~8MB stack - always heap-allocate
// them (never as a local/stack variable, which would overflow the stack immediately)
// and delete promptly so at most one such object is alive at a time, the same way
// the real engine only ever uses `new lookup_table` (see ascaniusfish.cpp).

static void expect(bool condition, const std::string& message)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

// TT_entry no longer carries a full board, only a zobrist hash - that hash IS
// the position's identity now, so building a synthetic entry just needs the
// hash it should be found/routed under.
static TT_entry make_entry(uint64_t hash, int depth, int bound_type=0)
{
    TT_entry entry;
    entry.initialized = true;
    entry.zobrist_hash = hash;
    entry.pv_line = PV_Line();
    entry.pv_line.depth = depth;
    entry.pv_line.eval = depth * 10;
    entry.pv_line.bound_type = bound_type;
    entry.is_from_opening_book = false;
    return entry;
}

// is_retrivable_eval() still takes a BB - build a throwaway one carrying only
// the zobrist hash we want to look up.
static BB key_for(uint64_t hash)
{
    BB key = BB();
    key.zobrist_hash = hash;
    return key;
}

static void test_base_lookup_table_unchanged()
{
    lookup_table* table = new lookup_table(); // lookup_table_base<15,8> alias
    TT_entry entry = make_entry(/*hash=*/111, /*depth=*/4);
    table->insert(entry);

    BB key = key_for(111);
    TT_readout readout = table->is_retrivable_eval(&key, 0);
    expect(readout.is_found, "base lookup_table: inserted entry should be found");
    expect(readout.pv_line.depth==4, "base lookup_table: retrieved depth should match inserted depth");
    delete table;
}

static void test_round_trip(const std::string& path)
{
    TT_entry a = make_entry(/*hash=*/1, /*depth=*/6);
    TT_entry b = make_entry(/*hash=*/2, /*depth=*/9);
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

        BB key_a = key_for(1);
        BB key_b = key_for(2);
        TT_readout readout_a = loaded->is_retrivable_eval(&key_a, 0);
        TT_readout readout_b = loaded->is_retrivable_eval(&key_b, 0);
        expect(readout_a.is_found && readout_a.pv_line.depth==6, "round-trip: entry a should round-trip with its depth");
        expect(readout_b.is_found && readout_b.pv_line.depth==9, "round-trip: entry b should round-trip with its depth");
        delete loaded;
    }
}

static void test_monotonic_load(const std::string& path)
{
    TT_entry shallow = make_entry(/*hash=*/5, /*depth=*/3);
    TT_entry deep = make_entry(/*hash=*/5, /*depth=*/8);
    BB key = key_for(5); // same identity as shallow, only depth differs

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
        TT_entry shallow2 = make_entry(/*hash=*/5, /*depth=*/1);
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
    // PTT's base is lookup_table_base<16, 8> - bucket_size is 8, and get_hash()
    // routes by the low 16 bits of zobrist_hash. Force 9 DISTINCT positions
    // (distinct full hash, so insert() never treats them as "already in table")
    // into the SAME bucket by sharing those low 16 bits, with strictly
    // increasing depth. The 9th insert should evict the depth-1 entry (lowest
    // value under PTT's depth-only scoring), not whichever the base TT's
    // move-number-weighted policy would pick.
    PTT* ptt = new PTT();
    const uint64_t shared_low_bits = 777;
    auto hash_for = [&](int i){ return shared_low_bits | (uint64_t(i) << 16); };

    for(int i=1; i<=8; i++)
    {
        ptt->insert(make_entry(hash_for(i), /*depth=*/i));
    }

    TT_entry newcomer = make_entry(hash_for(9), /*depth=*/5);
    ptt->insert(newcomer);

    BB key_depth1 = key_for(hash_for(1));
    BB key_newcomer = key_for(hash_for(9));

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
