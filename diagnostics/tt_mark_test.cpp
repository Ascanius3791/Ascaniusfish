// OWNERSHIP=Claude
// A user's mark (#101) is never lost (#100): on one bucket of UCI_Table,
// marked entries stay through mate claims of other positions, a marked
// candidate that finds no unmarked slot, and every result for the marked
// position itself, which keeps the higher mark. Exit 1 on a failure.
//
//   g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/tt_mark_test diagnostics/tt_mark_test.cpp
//   ./diagnostics/tt_mark_test
#include "../lib/uci.hpp"

static int failures = 0;
static void check(bool ok, const std::string& what)
{
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    failures += !ok;
}

// The k-th key of bucket 5: the bucket index is the key's low bits.
static uint64_t key(int k) { return ((uint64_t)(k+1) << 32) | 5; }

static TT_entry entry(int k, int depth, int eval, int bound_type = 0)
{
    TT_entry e;
    e.zobrist_hash = key(k);
    e.initialized = true;
    e.pv_line.eval = eval;
    e.pv_line.depth = depth;
    e.pv_line.bound_type = bound_type;
    return e;
}

static int mark_of(UCI_Table& t, int k)
{
    const TT_slot* s = t.find_slot(key(k));
    return s ? tt_mark(s->pv_line) : -1;
}

int main()
{
    UCI_Table* t = new UCI_Table();
    const int mate = INT_MAX-5;

    // Seven marked entries and one unmarked mate claim fill the bucket.
    for(int k=0;k<7;k++)
    check(t->import_marked(entry(k, 4, 10*k), (int16_t)(k+1)), "mark " + std::to_string(k+1) + " stored");
    t->insert(entry(7, 30, mate));
    // Unmarked mate claims of other positions, deeper than every mark: each
    // takes the unmarked slot, never a marked one.
    for(int k=8;k<20;k++)
    t->insert(entry(k, 40+k, mate));
    bool all = true;
    for(int k=0;k<7;k++)
    all = all && mark_of(*t, k)==k+1;
    check(all, "12 deeper mate claims of other positions evict no mark");
    check(mark_of(*t, 19)==0, "the last claim holds the unmarked slot");

    // Fill the last slot with a mark: a higher mark of a new position finds
    // no unmarked slot and is not stored.
    check(t->import_marked(entry(20, 4, 0), 8), "mark 8 takes the unmarked slot");
    check(!t->import_marked(entry(21, 60, mate), 900), "mark 900 of a new position, bucket all marked: not stored");
    t->insert(entry(22, 99, mate));
    all = true;
    for(int k=0;k<7;k++)
    all = all && mark_of(*t, k)==k+1;
    check(all && mark_of(*t, 20)==8, "all eight marks stay");

    // The marked position itself: a deeper mate claim replaces its result and
    // keeps the mark; a higher mark raises it, a lower one does not lower it.
    t->insert(entry(2, 50, mate));
    const TT_slot* s = t->find_slot(key(2));
    check(s && s->pv_line.eval==mate && s->pv_line.depth==50 && tt_mark(s->pv_line)==3, "a mate claim over mark 3: the claim, mark 3");
    t->insert(entry(2, 60, 5, -1));
    check(mark_of(*t, 2)==3, "a deeper bound after it: mark 3");
    t->import_marked(entry(2, 1, 0), 40);
    check(mark_of(*t, 2)==40 && t->find_slot(key(2))->pv_line.eval==mate, "mark 40 over it: mark 40, the claim stays");
    t->import_marked(entry(2, 99, 7), 2);
    check(mark_of(*t, 2)==40, "mark 2 over it: still mark 40");
    t->store_proven(key(2), entry(2, 1, INT_MAX-3).pv_line);
    check(mark_of(*t, 2)==40, "a verified root mate over it: mark 40");

    int marked = 0;
    t->demote_unmarked(marked);
    check(marked==8, "Return to root keeps the eight marked");

    std::cout << (failures ? "FAILED " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
