// OWNERSHIP=Claude
// Correctness checks for the chunked PV_Line (lib/move_generation.hpp) and its
// extension pool (lib/pv_pool.hpp). PV_Line owns heap-ish chunks now, so the
// things that can go wrong are ownership bugs: a copy that aliases instead of
// cloning, an assignment that drops its old chain without returning it (pool
// leak) or returns it twice, a move that leaves the source owning what it gave
// away, and lines that straddle chunk boundaries reading the wrong slot.
// Every case asserts the pool is back to zero chunks in use at the end, which
// is what catches leaks and double-releases.
//
// Build twice - once normally, once with a tiny pool to exercise exhaustion:
//   g++ -O2 -w -DNDEBUG -o diagnostics/pv_chunk_test diagnostics/pv_chunk_test.cpp
//   g++ -O2 -w -DNDEBUG -DPV_POOL_CAP=4 -o diagnostics/pv_chunk_test_tiny diagnostics/pv_chunk_test.cpp
#include "../ascaniusfish.hpp"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string& message)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        failures++;
    }
}

// A move whose from-square encodes i, so a line's contents are checkable.
static Move marker(int i) { return Move(i%64, (i*7+13)%64); }

static PV_Line line_of(int n)
{
    PV_Line line;
    for(int i=0;i<n;i++)
    if(line.set_move(i, marker(i)))
    line.current_lenght = i+1;
    return line;
}

static bool holds(const PV_Line& line, int n)
{
    if(line.current_lenght != n)
    return false;
    for(int i=0;i<n;i++)
    if(!(line.at(i)==marker(i)))
    return false;
    return true;
}

static int in_use() { return pv_extension_pool().in_use(); }

int main()
{
    std::cout << "sizeof(Move)         = " << sizeof(Move) << "\n";
    std::cout << "sizeof(PV_Line)      = " << sizeof(PV_Line) << "\n";
    std::cout << "sizeof(PV_extension) = " << sizeof(PV_extension) << "\n";
    std::cout << "sizeof(TT_entry)     = " << sizeof(TT_entry) << "\n";
    std::cout << "PV_CHUNK = " << PV_CHUNK << "   pool capacity = "
              << pv_extension_pool().capacity() << "\n\n";

    expect(in_use()==0, "pool should start empty");

    // (1) a line that fits inline takes no chunk at all
    {
        PV_Line line = line_of(PV_CHUNK);
        expect(holds(line, PV_CHUNK), "(1) inline line contents");
        expect(line.extension==nullptr, "(1) inline line must not allocate");
        expect(in_use()==0, "(1) no chunks for an inline line");
    }
    expect(in_use()==0, "(1) pool empty after scope");

    // (2) one move past inline takes exactly one chunk, and frees it
    {
        PV_Line line = line_of(PV_CHUNK+1);
        expect(holds(line, PV_CHUNK+1), "(2) contents across the first boundary");
        expect(line.extension!=nullptr, "(2) should have allocated");
        expect(in_use()==1, "(2) exactly one chunk");
    }
    expect(in_use()==0, "(2) destructor returned the chunk");

    // (3) a long line spanning several chunks
    {
        const int n = PV_CHUNK*4 + 3;
        PV_Line line = line_of(n);
        expect(holds(line, n), "(3) contents across several chunks");
        expect(in_use()==4, "(3) expected 4 chunks, got " + std::to_string(in_use()));
    }
    expect(in_use()==0, "(3) pool empty after scope");

    // (4) copy is a deep copy: source and copy must not share chunks
    {
        const int n = PV_CHUNK*2 + 1;
        PV_Line a = line_of(n);
        PV_Line b = a;
        expect(holds(b, n), "(4) copy has the same contents");
        expect(b.extension!=a.extension, "(4) copy must not alias the source's chain");
        expect(in_use()==4, "(4) both lines hold their own chunks");
        a.set_move(PV_CHUNK, Move(1,2));
        expect(b.at(PV_CHUNK)==marker(PV_CHUNK), "(4) writing a must not change b");
    }
    expect(in_use()==0, "(4) pool empty after scope");

    // (5) copy-assignment must release the target's old chain exactly once
    {
        PV_Line a = line_of(PV_CHUNK*3);
        PV_Line b = line_of(PV_CHUNK*2);
        expect(in_use()==3, "(5) 2+1 chunks before assignment");
        b = a;
        expect(holds(b, PV_CHUNK*3), "(5) assigned contents");
        expect(in_use()==4, "(5) b's old chain returned, got " + std::to_string(in_use()));
    }
    expect(in_use()==0, "(5) pool empty after scope");

    // (6) self-assignment must not destroy the line
    {
        PV_Line a = line_of(PV_CHUNK*2);
        PV_Line& ref = a;
        a = ref;
        expect(holds(a, PV_CHUNK*2), "(6) self copy-assignment preserved contents");
        a = std::move(ref);
        expect(holds(a, PV_CHUNK*2), "(6) self move-assignment preserved contents");
    }
    expect(in_use()==0, "(6) pool empty after scope");

    // (7) move steals rather than clones, and leaves the source owning nothing
    {
        const int n = PV_CHUNK*2+2;
        PV_Line a = line_of(n);
        const int before = in_use();
        PV_Line b = std::move(a);
        expect(holds(b, n), "(7) moved-to contents");
        expect(in_use()==before, "(7) move must not allocate");
        expect(a.extension==nullptr && a.current_lenght==0, "(7) moved-from owns nothing");
    }
    expect(in_use()==0, "(7) pool empty after scope");

    // (8) PV_Line(move,depth,&child): prepend shifts every move by one, so the
    //     result crosses chunk boundaries at different places than the child did
    {
        const int n = PV_CHUNK*2+5;
        PV_Line child = line_of(n);
        PV_Line parent(marker(999), 7, &child);
        expect(parent.current_lenght==n+1, "(8) prepended length");
        expect(parent.at(0)==marker(999), "(8) prepended move is first");
        bool shifted = true;
        for(int i=0;i<n;i++)
        if(!(parent.at(i+1)==marker(i)))
        shifted = false;
        expect(shifted, "(8) child's moves shifted by one");
        expect(parent.depth==7, "(8) depth kept");
    }
    expect(in_use()==0, "(8) pool empty after scope");

    // (9) first_n flattens, clamps to current_lenght, and at() is safe past the end
    {
        PV_Line line = line_of(PV_CHUNK+4);
        std::vector<Move> some = line.first_n(PV_CHUNK+2);
        expect((int)some.size()==PV_CHUNK+2, "(9) first_n size");
        expect(some[PV_CHUNK+1]==marker(PV_CHUNK+1), "(9) first_n crosses the boundary");
        expect((int)line.first_n(1000).size()==PV_CHUNK+4, "(9) first_n clamps");
        Move past = line.at(1000);
        expect(past==Move(), "(9) at() past the end is a default Move");
    }
    expect(in_use()==0, "(9) pool empty after scope");

    // (10) MAX_PV_Lenght is still the hard cap
    {
        PV_Line line;
        expect(!line.set_move(MAX_PV_Lenght, Move(1,2)), "(10) set_move refuses >= MAX_PV_Lenght");
        // The last valid index needs MAX_PV_Lenght/PV_CHUNK-1 chunks, so only
        // assert it succeeds when the pool is actually big enough to supply them.
        const int chunks_needed = MAX_PV_Lenght/PV_CHUNK - 1;
        if(pv_extension_pool().capacity() >= chunks_needed)
        expect(line.set_move(MAX_PV_Lenght-1, Move(1,2)), "(10) set_move accepts the last index");
        line.clear_extension();
    }
    expect(in_use()==0, "(10) pool empty after scope");

    // (11) TT_entry round-trips through copy and move like any value
    {
        TT_entry entry;
        entry.initialized = true;
        entry.zobrist_hash = 0xabcdef;
        entry.pv_line = line_of(PV_CHUNK*2+1);
        TT_entry copy = entry;
        expect(holds(copy.pv_line, PV_CHUNK*2+1), "(11) TT_entry copy contents");
        expect(copy.pv_line.extension!=entry.pv_line.extension, "(11) TT_entry copy must not alias");
        TT_entry moved = std::move(copy);
        expect(holds(moved.pv_line, PV_CHUNK*2+1), "(11) TT_entry move contents");
    }
    expect(in_use()==0, "(11) pool empty after scope");

    // (12) exhaustion degrades instead of lying: current_lenght only ever counts
    //      moves that are really there, and truncated says the line is short.
    //      Only meaningful in the tiny-pool build.
    {
        const int cap = pv_extension_pool().capacity();
        std::vector<PV_Line> lines;
        for(int i=0;i<cap+4;i++)
        lines.push_back(line_of(PV_CHUNK*2));
        bool any_truncated = false, all_consistent = true;
        for(const PV_Line& line : lines)
        {
            if(line.truncated)
            any_truncated = true;
            for(int k=0;k<line.current_lenght;k++)
            if(!(line.at(k)==marker(k)))
            all_consistent = false;
        }
        expect(all_consistent, "(12) every reported move is really stored");
        expect(any_truncated, "(12) some line should be flagged truncated once the pool runs dry");
        expect(in_use()<=cap, "(12) never hands out more than capacity");
    }
    expect(in_use()==0, "(12) pool empty after scope");

    // (13) the PTT file format: a TT_entry is no longer a flat blob, so a line
    //      longer than one chunk has to survive save() -> load() move for move.
    {
        const int n = PV_CHUNK*2 + 5;
        const std::string path = "/tmp/pv_chunk_ptt_test.bin";
        {
            PTT* table = new PTT();
            TT_entry entry;
            entry.initialized = true;
            entry.zobrist_hash = 0x1234567890abcdefULL;
            entry.pv_line = line_of(n);
            entry.pv_line.depth = 11;
            entry.pv_line.eval = -4242;
            entry.pv_line.bound_type = 0;
            table->insert(entry);
            expect(table->save(path), "(13) save a long-PV entry");
            delete table;
        }
        {
            PTT* table = new PTT();
            expect(table->load(path), "(13) load it back");
            BB key;
            key.zobrist_hash = 0x1234567890abcdefULL;
            TT_readout readout = table->is_retrivable_eval(&key, 0);
            expect(readout.is_found, "(13) entry found after reload");
            expect(holds(readout.pv_line, n),
                   "(13) all " + std::to_string(n) + " moves survived the round trip, got length "
                   + std::to_string(readout.pv_line.current_lenght));
            expect(readout.pv_line.depth==11 && readout.pv_line.eval==-4242
                   && readout.pv_line.bound_type==0, "(13) scalars survived");
            delete table;
        }
        std::remove(path.c_str());
    }
    expect(in_use()==0, "(13) pool empty after scope");

    std::cout << "\npool high water: " << pv_extension_pool().high_water()
              << " / " << pv_extension_pool().capacity() << "\n";
    if(failures)
    {
        std::cout << failures << " check(s) FAILED" << std::endl;
        return 1;
    }
    std::cout << "all PV_Line chunk/ownership checks passed" << std::endl;
    return 0;
}
