// OWNERSHIP=Claude
#ifndef SEARCH_CONTROL_HPP
#define SEARCH_CONTROL_HPP

#include <atomic>
#include <chrono>

// Lets a running search be aborted from outside (UCI "stop", a movetime
// deadline). minimax() and minimax_tactical() call poll_search_abort() once
// per node; every 256th node it checks the stop flag and deadline and throws
// search_aborted, which unwinds the whole tree back to the caller of the root
// search. Nothing is stored in the TT for a node that was unwound, so the
// table stays consistent; the caller simply keeps the result of the last
// fully completed iteration.

struct search_aborted {};

inline std::atomic<bool> stop_search_flag{false};

// steady_clock time in ns since epoch after which the search must stop;
// 0 = no deadline. Set before a search starts.
inline std::atomic<long long> search_deadline_ns{0};

inline long long steady_now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline bool search_stop_requested()
{
    if(stop_search_flag.load(std::memory_order_relaxed))
    return true;
    long long deadline = search_deadline_ns.load(std::memory_order_relaxed);
    if(deadline && steady_now_ns() >= deadline)
    {
        stop_search_flag.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

// Nodes visited by minimax() + minimax_tactical(); also the UCI node count.
inline long long search_nodes = 0;

inline void poll_search_abort()
{
    if((++search_nodes & 255)==0 && search_stop_requested())
    throw search_aborted();
}

#endif // SEARCH_CONTROL_HPP
