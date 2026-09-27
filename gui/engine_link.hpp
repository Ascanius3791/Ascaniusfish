// OWNERSHIP=Claude
// One ./ascaniusfish_uci process for the GUI, driven from a worker thread.
//
// The HTTP loop must answer page loads and "stop" while the engine thinks, so
// nothing here ever blocks it: the server hands over a Search_Request, the
// worker does the whole UCI exchange, and the server picks the answer up on a
// later poll() iteration. The worker touches no Session and no socket — it
// only calls on_update() to wake the poll loop, which then does the reading.
//
// Engine access is UCI over pipes (tools/uci_engine.hpp), never a call into the
// search: the GUI is a UCI client, so every later engine feature arrives the
// same way it would for any other GUI.
#ifndef GUI_ENGINE_LINK_HPP
#define GUI_ENGINE_LINK_HPP
#include "../tools/uci_engine.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

constexpr const char* ENGINE_PATH = "./ascaniusfish_uci";
constexpr long long ENGINE_GRACE_MS = 5000;        // past movetime before the engine counts as hung
constexpr long long ENGINE_MAX_WAIT_MS = 600000;   // a fixed-depth search may take a while; not forever
constexpr long long ENGINE_ANALYSIS_WAIT_MS = 86400000;  // "go infinite" ends on "stop", not on a clock
constexpr int ENGINE_DEFAULT_DEPTH = 6;

// What the engine is told about one move. Fixed depth, a fixed time per move,
// or — for Analyse mode — no limit at all; the clock fields are here so M4 only
// has to fill them in rather than reshape everything that carries a limit around.
struct Go_Limits
{
    int depth = ENGINE_DEFAULT_DEPTH;
    long long movetime_ms = 0;
    long long wtime_ms = 0, btime_ms = 0, winc_ms = 0, binc_ms = 0;
    bool infinite = false;   // think until "stop": what Analyse mode asks for

    // The limits an analysis search runs under. It is ended by the position
    // changing or the toggle going off, never by a clock.
    static Go_Limits analysis()
    {
        Go_Limits limits;
        limits.depth = 0;
        limits.infinite = true;
        return limits;
    }

    bool clocked() const { return wtime_ms>0 || btime_ms>0; }

    // Which of the four the engine will be given.
    const char* kind() const
    {
        return infinite ? "infinite" : clocked() ? "clock" : movetime_ms>0 ? "movetime" : "depth";
    }

    long long value() const { return infinite || clocked() ? 0 : movetime_ms>0 ? movetime_ms : depth; }

    std::string go_command() const
    {
        if(infinite)
        return "go infinite";
        if(clocked())
        return "go wtime " + std::to_string(wtime_ms) + " btime " + std::to_string(btime_ms)
             + " winc " + std::to_string(winc_ms) + " binc " + std::to_string(binc_ms);
        if(movetime_ms>0)
        return "go movetime " + std::to_string(movetime_ms);
        return "go depth " + std::to_string(depth>0 ? depth : ENGINE_DEFAULT_DEPTH);
    }

    // How long to wait for a bestmove before calling the engine hung.
    long long deadline_ms() const
    {
        if(infinite)
        return ENGINE_ANALYSIS_WAIT_MS;   // only a dead engine (EOF) ends the wait early
        if(movetime_ms>0)
        return movetime_ms+ENGINE_GRACE_MS;
        if(clocked())
        return std::max(wtime_ms, btime_ms)+ENGINE_GRACE_MS;
        return ENGINE_MAX_WAIT_MS;
    }
};

// One position to think about: the root the engine is given plus the moves
// played from it, so it sees the whole game (and with it the repetition rule).
struct Search_Request
{
    std::string start_fen;
    std::vector<std::string> moves;
    Go_Limits limits;
    bool new_game = false;   // send "ucinewgame" first
};

struct Search_Result
{
    long long token = 0;     // the search this answers; a stale one is dropped
    std::string bestmove;    // "" when the engine gave none
    Search_Info info;        // last completed iteration
    std::string error;       // set instead of bestmove when the engine died or hung
};

class Engine_Link
{
    public:
    std::string path = ENGINE_PATH;
    std::function<void()> on_update;   // called from the worker thread; must only wake the loop

    ~Engine_Link()
    {
        {
            std::lock_guard<std::mutex> lock(m);
            quitting = true;
        }
        wake.notify_all();
        if(worker.joinable())
        worker.join();
        engine.stop(200);
    }

    // Queues a search and returns its token, or 0 while one is still running.
    long long start_search(const Search_Request& request)
    {
        std::unique_lock<std::mutex> lock(m);
        if(searching)
        return 0;
        pending = request;
        token = ++next_token;
        searching = true;
        has_result = false;
        progress_seq = 0;
        taken_progress = 0;
        progress = Search_Info();
        lock.unlock();
        if(!worker.joinable())
        worker = std::thread([this] { run(); });
        wake.notify_all();
        return token;
    }

    // Asks the engine to stop. Its bestmove still arrives; the caller drops it
    // by no longer awaiting that token.
    void abort()
    {
        std::lock_guard<std::mutex> lock(m);
        if(searching)
        send_locked("stop");
    }

    bool searching_now() const
    {
        std::lock_guard<std::mutex> lock(m);
        return searching;
    }

    // Takes the finished search, once. False while none is waiting.
    bool take_result(Search_Result& out)
    {
        std::lock_guard<std::mutex> lock(m);
        if(!has_result)
        return false;
        out = result;
        has_result = false;
        return true;
    }

    // The newest "info depth" line of the running search, once per new line.
    bool take_progress(Search_Info& out)
    {
        std::lock_guard<std::mutex> lock(m);
        if(!progress_seq || progress_seq==taken_progress)
        return false;
        taken_progress = progress_seq;
        out = progress;
        return true;
    }

    private:
    mutable std::mutex m;
    std::condition_variable wake;
    std::thread worker;
    Engine engine;                     // only the worker starts/reads it; writes go through send_locked
    Search_Request pending;
    Search_Result result;
    Search_Info progress;
    long long next_token = 0, token = 0;
    long long progress_seq = 0, taken_progress = 0;
    bool searching = false, has_result = false, quitting = false;

    // A write to the engine's stdin with `m` held, so an abort's "stop" can
    // never interleave with the worker's "position"/"go".
    void send_locked(const std::string& line)
    {
        engine.send(line);
    }

    void run()
    {
        for(;;)
        {
            Search_Request request;
            std::unique_lock<std::mutex> lock(m);
            wake.wait(lock, [this] { return quitting || (searching && !has_result); });
            if(quitting)
            return;
            request = pending;
            long long my_token = token;

            Search_Result answer;
            answer.token = my_token;
            if(engine.pid<0)
            {
                engine.path = path;
                if(!engine.start())
                answer.error = "cannot start " + path + " (run make first, from the repo root)";
                else
                request.new_game = true;   // a fresh process has no game to forget
            }
            if(answer.error.empty())
            {
                if(request.new_game)
                send_locked("ucinewgame");
                std::string position = "position fen " + request.start_fen;
                if(!request.moves.empty())
                position += " moves";
                for(const std::string& move : request.moves)
                position += " " + move;
                send_locked(position);
                send_locked(request.limits.go_command());
                lock.unlock();
                read_bestmove(request.limits, my_token, answer);
                lock.lock();
            }
            result = answer;
            has_result = true;
            searching = false;
            lock.unlock();
            // An engine that died leaves a dead process behind; the next search
            // starts a new one rather than writing into a closed pipe forever.
            if(!answer.error.empty())
            engine.stop(200);
            if(on_update)
            on_update();
        }
    }

    // Reads until "bestmove", publishing every finished iteration as progress.
    void read_bestmove(const Go_Limits& limits, long long my_token, Search_Result& answer)
    {
        long long deadline = now_ms()+limits.deadline_ms();
        std::string line;
        while(engine.read_line(line, deadline))
        {
            Search_Info info;
            if(parse_info(line, info))
            {
                {
                    std::lock_guard<std::mutex> lock(m);
                    progress = info;
                    progress_seq++;
                }
                answer.info = info;
                if(on_update)
                on_update();
            }
            else if(line.compare(0, 9, "bestmove ")==0)
            {
                std::istringstream in(line.substr(9));
                in >> answer.bestmove;
                if(answer.bestmove=="0000" || answer.bestmove=="(none)")
                answer.bestmove.clear();
                if(answer.bestmove.empty())
                answer.error = "the engine found no move to play";
                return;
            }
        }
        answer.error = now_ms()<deadline ? "the engine process stopped answering"
                                        : "the engine did not answer in time";
    }
};

#endif // GUI_ENGINE_LINK_HPP
