// OWNERSHIP=Claude
// Does Engine_Link::abort() still end a search that was aborted before its
// "go" ever reached the engine?
//
// The GUI starts an analysis with "go infinite" and stops it by sending
// "stop". start_search() only queues the search — the worker thread sends the
// "position" and the "go" a moment later — so an abort in that moment used to
// send its "stop" first, where the engine reads it with nothing to search and
// throws it away. The "go infinite" that followed then had nothing left to end
// it: the engine never sends a bestmove, Engine_Link stays `searching`, every
// later start_search() returns 0, and the GUI waits for an answer that cannot
// come. That is a permanent hang, and it is likeliest right after a position
// the engine answers instantly — a proven mate, say — because those are the
// searches that sit in "go infinite" waiting to be stopped.
//
// This aborts in exactly that window (the very first search of a link, whose
// worker thread still has to be created and the engine process spawned) and
// gives the answer a generous deadline. Before the fix it times out; after it,
// the bestmove arrives.
//
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -pthread -o diagnostics/gui_abort_race_probe
//       diagnostics/gui_abort_race_probe.cpp
//
// Run it from the repo root, so ./ascaniusfish_uci is found.
#include "../gui/engine_link.hpp"
#include <cstdio>

static const char* const PROBE_FEN = "rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq g3 0 2";
constexpr long long ANSWER_TIMEOUT_MS = 15000;

// One aborted "go infinite": true when its bestmove came back.
static bool aborted_search_answers(const char* what, bool abort_before_go)
{
    Engine_Link link;
    Search_Request request;
    request.start_fen = PROBE_FEN;
    request.limits = Go_Limits::analysis();
    request.new_game = true;

    long long token = link.start_search(request);
    if(!token)
    {
        std::printf("  %-22s FAIL  the link refused the first search\n", what);
        return false;
    }
    if(!abort_before_go)
    {
        // Let the worker get the "go" out first: the case that always worked.
        while(!link.searching_now())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    link.abort();

    Search_Result result;
    long long deadline = now_ms()+ANSWER_TIMEOUT_MS;
    while(now_ms()<deadline)
    {
        if(link.take_result(result))
        {
            std::printf("  %-22s ok    bestmove \"%s\"%s%s after %lld ms\n", what,
                        result.bestmove.c_str(), result.error.empty() ? "" : ", error: ",
                        result.error.c_str(), ANSWER_TIMEOUT_MS-(deadline-now_ms()));
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("  %-22s FAIL  no answer in %lld ms - the search was never stopped\n",
                what, ANSWER_TIMEOUT_MS);
    return false;
}

int main()
{
    std::signal(SIGPIPE, SIG_IGN);   // a dead engine must be an error, not a kill
    std::printf("Engine_Link: does an aborted \"go infinite\" always answer?\n");
    bool ok = aborted_search_answers("abort after the go", false);
    ok &= aborted_search_answers("abort before the go", true);
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
