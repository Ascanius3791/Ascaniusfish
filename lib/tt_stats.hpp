// OWNERSHIP=Claude
// TT discard statistics (issue #11): how often does the transposition table
// throw away a position that the search asks for again later?
//
// Only compiled with -DTT_STATS (tools/tt_stats, `make tt-stats`). In every
// other build TT_STATS_HOOK(...) expands to nothing and none of the code
// below exists, so the engine carries no ghost table and no extra work.
//
// Mechanism: a ghost table (zobrist hash -> what was lost) records every
// entry the TT discards, i.e. an entry evicted from a full bucket or a new
// entry rejected as the least valuable one. Re-storing the position erases
// its ghost. On every TT miss the ghost table is checked: a hit means "this
// position was asked for again, but we had thrown it away" (a regret probe).
// Counts are counterfactual - with the entry present the search tree would
// have been different - so read them as potential, not exact nodes lost.
// All tables share these globals; tools/tt_stats uses exactly one.
#ifndef TT_STATS_HPP
#define TT_STATS_HPP

#ifdef TT_STATS
#define TT_STATS_HOOK(...) do { __VA_ARGS__; } while(0)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace tt_stats
{
    enum Discard_Reason { EVICTED, REJECTED };

    constexpr int DEPTH_ROWS = 64;  // depths >= 63 share the last row

    struct Ghost
    {
        uint64_t discarded_at;  // insertions counter at the discard
        int16_t depth;
        int8_t bound_type;
        bool requested;         // asked for at least once since the discard
    };

    struct Depth_Row
    {
        long long stored = 0;           // new entries written to a slot (deepening in place keeps the first depth)
        long long evicted = 0;          // overwritten in a full bucket
        long long rejected = 0;         // not stored: least valuable in its bucket
        long long lost_then_asked = 0;  // discarded entries requested at least once later
        long long probes_deep_exact = 0;  // regret probes, ghost depth >= requested, exact bound
        long long probes_deep_bound = 0;  // same, lower/upper bound
        long long probes_shallow = 0;     // ghost shallower than requested: move-ordering hint only
        std::vector<uint64_t> reuse_distances;  // insertions between discard and first request
    };

    inline std::unordered_map<uint64_t, Ghost> ghosts;
    inline Depth_Row rows[DEPTH_ROWS];
    inline uint64_t insertions = 0;  // counts on_store + on_discard(REJECTED): every attempt that needed a slot
    inline long long filled = 0;     // occupied slots since the last reset (stores into a free slot)
    inline long long probes[2] = {0, 0}, hits[2] = {0, 0}, misses[2] = {0, 0};  // [0] quiescence (depth 0), [1] main search

    inline int row_of(int depth)
    {
        return std::min(std::max(depth, 0), DEPTH_ROWS-1);
    }

    template<class Entry>
    inline void on_store(const Entry& entry)
    {
        insertions++;
        filled++;  // an eviction (on_discard EVICTED) follows when the slot was taken
        rows[row_of(entry.pv_line.depth)].stored++;
        ghosts.erase(entry.zobrist_hash);
    }

    template<class Entry>
    inline void on_discard(const Entry& entry, Discard_Reason reason)
    {
        if(reason==REJECTED)
        insertions++;
        else
        filled--;
        Depth_Row& row = rows[row_of(entry.pv_line.depth)];
        (reason==EVICTED ? row.evicted : row.rejected)++;
        ghosts[entry.zobrist_hash] = Ghost{insertions, (int16_t)entry.pv_line.depth, (int8_t)entry.pv_line.bound_type, false};
    }

    inline void on_probe(uint64_t zobrist_hash, bool found, int requested_depth)
    {
        int kind = requested_depth>0;
        probes[kind]++;
        if(found)
        {
            hits[kind]++;
            return;
        }
        misses[kind]++;
        auto it = ghosts.find(zobrist_hash);
        if(it==ghosts.end())
        return;
        Ghost& ghost = it->second;
        Depth_Row& row = rows[row_of(ghost.depth)];
        if(ghost.depth<requested_depth)
        row.probes_shallow++;
        else if(ghost.bound_type==0)
        row.probes_deep_exact++;
        else
        row.probes_deep_bound++;
        if(!ghost.requested)
        {
            ghost.requested = true;
            row.lost_then_asked++;
            row.reuse_distances.push_back(insertions-ghost.discarded_at);
        }
    }

    // A table reset (new game) is not a premature discard: forget the ghosts,
    // keep the counters.
    inline void on_reset()
    {
        filled = 0;
        ghosts.clear();
    }

    // Cumulative statistics so far (also used for snapshots during a game).
    inline void print_stats(long long capacity)
    {
        std::printf("TT %.1f%% full (%lld of %lld entries), %llu insertions\n", 100.0*filled/capacity, filled, capacity, (unsigned long long)insertions);
        const char* kind_names[2] = {"quiescence ", "main search"};
        for(int k=1; k>=0; k--)
        std::printf("%s probes %10lld  hits %10lld (%5.1f%%)  misses %10lld\n", kind_names[k], probes[k], hits[k],
            probes[k] ? 100.0*hits[k]/probes[k] : 0.0, misses[k]);
        long long regret = 0;
        for(const Depth_Row& r : rows)
        regret += r.probes_deep_exact+r.probes_deep_bound+r.probes_shallow;
        std::printf("misses of discarded positions %lld (%.2f%% of all misses), ghosts alive %zu\n\n",
            regret, misses[0]+misses[1] ? 100.0*regret/(misses[0]+misses[1]) : 0.0, ghosts.size());
        std::printf("depth |  new slot   evicted  rejected | lost->asked   %%lost | regret probes: deep-exact deep-bound  shallow | reuse dist median\n");
        for(int d=0; d<DEPTH_ROWS; d++)
        {
            Depth_Row& r = rows[d];
            long long lost = r.evicted+r.rejected;
            if(r.stored+lost==0)
            continue;
            char median[32] = "-";
            if(!r.reuse_distances.empty())
            {
                auto mid = r.reuse_distances.begin()+r.reuse_distances.size()/2;
                std::nth_element(r.reuse_distances.begin(), mid, r.reuse_distances.end());
                std::snprintf(median, sizeof median, "%llu", (unsigned long long)*mid);
            }
            std::printf("%3d%s | %9lld %9lld %9lld | %11lld %6.1f%% |             %10lld %10lld %8lld | %17s\n",
                d, d==DEPTH_ROWS-1 ? "+" : " ", r.stored, r.evicted, r.rejected, r.lost_then_asked,
                lost ? 100.0*r.lost_then_asked/lost : 0.0, r.probes_deep_exact, r.probes_deep_bound, r.probes_shallow, median);
        }
    }

    inline void print_legend()
    {
        std::printf("depth: search depth of the entry when it was stored/discarded. new slot: entries written to\n"
                    "a slot at this depth (an entry deepened in place by iterative deepening keeps its first depth).\n"
                    "lost->asked: discarded entries of this depth requested again later (%%lost: share of all discarded).\n"
                    "deep-*: the lost entry was at least as deep as the request (exact: could have returned; bound: could\n"
                    "have tightened alpha/beta at equal depth). shallow: it would only have ordered moves.\n"
                    "reuse dist: insertions between the discard and the first request; far above capacity means the\n"
                    "table is too small, far below it means the replacement policy threw out something needed soon.\n");
    }
}

#else
#define TT_STATS_HOOK(...) do {} while(0)
#endif

#endif // TT_STATS_HPP
