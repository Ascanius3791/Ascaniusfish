// OWNERSHIP=Claude
#ifndef PV_POOL_HPP
#define PV_POOL_HPP

#ifndef NDEBUG
#include <thread>
#include <iostream>
#include <cstdlib>
#endif

// Number of extension chunks reserved up front (override with -DPV_POOL_CAP=n).
// Sizing: a chunk is only taken for a PV line longer than PV_CHUNK moves, which
// diagnostics/pv_length_histogram.cpp measures at 0.33% of live transposition
// table entries in a KPvK depth-9 search and 0% from startpos at depth 5 - that
// search peaked at 10 chunks live. Scaled to a full table at TT_EXPONENT=15 the
// same rate is ~900 chunks, so 16384 is roughly 18x headroom at 2.2MB reserved
// once (PV_POOL_CAP*sizeof(PV_extension)). Erring low is cheap and erring high
// is not: running out only truncates a reported PV (see PV_Line::truncated),
// whereas the reservation is real memory taken from the search.
#ifndef PV_POOL_CAP
#define PV_POOL_CAP (1<<14)
#endif

// Fixed-size chunk allocator, used for PV_Line's extension chunks (see
// lib/move_generation.hpp).
//
// Every chunk handed out is exactly one T, so none of a general allocator's
// machinery is needed: no size classes, no per-block header, no lock, no trips
// to the OS. `storage` is one array reserved once in the constructor;
// acquire() either bumps `next_unused` or pops a chunk that was handed back,
// which is a load, a branch and a store. The freelist needs no storage of its
// own - a free chunk's contents are meaningless, so free chunks are threaded
// together through their own `next` pointer.
//
// The two properties this buys over new/delete, both of which are the actual
// reason it exists here:
//  - The footprint stays a compile-time constant, CAP*sizeof(T), instead of
//    becoming a function of which positions happen to get searched. And since
//    every slot is identical and interchangeable, a returned slot always fits
//    the next request, so nothing can fragment over a long self-play run.
//  - Reclaiming is allocator-free: handing a chain back is a pointer splice,
//    so clearing a full transposition table costs no free() calls at all
//    rather than one per live chunk.
//
// Exhaustion is not an error. acquire() returns nullptr, and the caller stores
// what fits and marks the line truncated (PV_Line::truncated). Callers must
// never treat nullptr as a reason to grow.
//
// NOT thread-safe, deliberately. Every PV_Line in the engine is built, copied
// and destroyed on whichever thread runs the search: UCI_Engine::search()
// builds the line, formats it for `info pv` and feeds the time manager all on
// the search thread, and the main thread only touches the stop flag (see
// src/uci.cpp). A debug build (`make debug`, which drops -DNDEBUG) records the
// first thread to use the pool and aborts if a second one shows up, so adding
// a parallel search fails loudly here instead of quietly corrupting the
// freelist.
template<typename T, int CAP>
class Chunk_pool
{
    T* storage = nullptr;
    T* free_head = nullptr;
    int next_unused = 0;
    int live = 0;
    int peak = 0;

#ifndef NDEBUG
    std::thread::id owner;
    bool owner_known = false;
    void check_owner()
    {
        if(!owner_known)
        {
            owner = std::this_thread::get_id();
            owner_known = true;
        }
        else if(owner != std::this_thread::get_id())
        {
            std::cerr << "Chunk_pool: used from a second thread - it is not thread-safe."
                      << std::endl;
            std::abort();
        }
    }
#else
    void check_owner() {}
#endif

    public:
    Chunk_pool() { storage = new T[CAP]; }
    ~Chunk_pool() { delete[] storage; }
    Chunk_pool(const Chunk_pool&) = delete;
    Chunk_pool& operator=(const Chunk_pool&) = delete;

    // nullptr means the pool is empty - store what fits and mark it truncated.
    T* acquire()
    {
        check_owner();
        T* chunk;
        if(free_head)
        {
            chunk = free_head;
            free_head = chunk->next;
        }
        else if(next_unused < CAP)
        {
            chunk = &storage[next_unused++];
        }
        else
        {
            return nullptr;
        }
        chunk->next = nullptr;
        live++;
        if(live > peak)
        peak = live;
        return chunk;
    }

    // Takes a whole chain back in one splice; the caller drops its pointer.
    void release_chain(T* chunk)
    {
        check_owner();
        while(chunk)
        {
            T* next = chunk->next;
            chunk->next = free_head;
            free_head = chunk;
            live--;
            chunk = next;
        }
    }

    // Invalidates EVERY chunk ever handed out, so it is only safe when nothing
    // holds one any more (every holder already released or was destroyed).
    // The transposition table does not use this - reset()/full_reset() already
    // walk all entries, so they release each entry's chain there instead.
    void reset_all()
    {
        check_owner();
        free_head = nullptr;
        next_unused = 0;
        live = 0;
    }

    int capacity() const { return CAP; }
    int in_use() const { return live; }
    int high_water() const { return peak; }
};

#endif // PV_POOL_HPP
