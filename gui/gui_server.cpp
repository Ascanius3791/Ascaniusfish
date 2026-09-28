// OWNERSHIP=Claude
// make gui: a chess board in the browser, served from this process.
//
//   ./gui/ascaniusfish_gui [key=value ...]
//
// Prints a http://localhost:<port> URL and serves gui/web/ there. The page draws
// the board with chessground; every rule — which moves exist, what they are
// called, whether the game is over — is decided here, in C++. The page holds no
// position of its own, so reloading it just asks for the state again.
//
// Options:
//   port=8173      first port to try; the next 20 are tried if it is taken
//   root=gui/web   directory the static files come from
//   fen=<fen>      starting position of the "main" session
//   engine=...     the UCI binary Play mode drives (default ./ascaniusfish_uci)
//
// Play, Analyse and Watch mode make this a UCI client of ./ascaniusfish_uci
// (gui/engine_link.hpp): the search runs on a worker thread, so a request never
// waits for it and the SSE stream carries the thinking indicator, the engine's
// move and — in Analyse mode — every iteration of a "go infinite" search on the
// position now on the board. Watch mode drives two of those processes, one per
// side, and plays them against each other; when the last page watching a
// session goes away its game is paused and its engines are let go, so a closed
// tab never leaves two searches running for nobody.
#include "http_server.hpp"
#include "session.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

constexpr int DEFAULT_PORT = 8173;
constexpr int MAX_DEPTH = 40;
constexpr long long MIN_CLOCK_BASE_MS = 100;            // permissive enough for hyperbullet's 1s
constexpr long long MAX_CLOCK_BASE_MS = 24LL*3600*1000; // a day
constexpr long long MAX_CLOCK_INC_MS = 3600*1000;       // an hour
constexpr int CLOCK_POLL_MS = 200;   // how often a running clock is checked for a flag fall
constexpr int IDLE_POLL_MS = 30000;  // the old fixed interval, when nothing is ticking

static Sessions sessions;
static Http_Server server;
static std::string web_root;
static std::string engine_path = ENGINE_PATH;

// A session's engine processes. Play and Analyse share one; Watch needs one
// per side, so the two self-play engines keep their own transposition tables
// and neither of them is also the analysis engine. Each starts on the first
// search it is asked for, so a session that never watches never forks the two
// extra processes.
enum { SLOT_SOLO = 0, SLOT_WHITE = 1, SLOT_BLACK = 2, N_SLOTS = 3 };

struct Engine_Set
{
    std::unique_ptr<Engine_Link> links[N_SLOTS];
    int active = -1;                         // the slot whose search is running
    long long awaiting = 0;                  // its token; an aborted search's answer is dropped
    int slot_game[N_SLOTS] = {-1, -1, -1};   // the game each slot last searched in
    long long alone_since = 0;               // when the last page left; 0 while one is watching
};

static std::map<std::string, std::unique_ptr<Engine_Set>> engines;

// Whether any session's clock is ticking right now, refreshed once a tick by
// collect_all() and read by the poll loop's timeout below — a running clock
// wants to be checked for a flag fall often, an idle server does not.
static bool any_clock_ticking = false;

static Engine_Set& engines_of(const Session& session)
{
    std::unique_ptr<Engine_Set>& set = engines[session.id];
    if(!set)
    set.reset(new Engine_Set);
    return *set;
}

static Engine_Link& link_of(Engine_Set& set, int slot)
{
    std::unique_ptr<Engine_Link>& link = set.links[slot];
    if(!link)
    {
        link.reset(new Engine_Link);
        link->path = engine_path;
        link->on_update = [] { server.wake(); };   // the only cross-thread call there is
    }
    return *link;
}

// Pushes the current position to every page watching this session.
static void broadcast(Session& session)
{
    server.publish(session.id, "state", session.state_json());
}

// Stops a running search and forgets its answer, for anything that changes the
// position under the engine: a new game, a FEN, a take back, a mode switch. A
// self-play game is stopped too — the engines were thinking about a position
// that is no longer the one on the board, and playing on from wherever the
// board ended up is not what anyone asked for.
static void abort_search(Session& session)
{
    Engine_Set& set = engines_of(session);
    if(set.active>=0 && set.links[set.active])
    set.links[set.active]->abort();
    set.active = -1;
    set.awaiting = 0;
    session.searching = Search_Kind::NONE;
    session.live_valid = false;
    session.watch_pause();
    session.clear_analysis();
}

// Asks an engine for whatever this session now wants thought about: its move in
// Play mode, the side to move's in Watch mode, the position on the board in
// Analyse mode. Returns at once; the answer arrives through on_tick. False when
// nothing is wanted, or when the engine is still finishing an aborted search —
// on_tick tries again as soon as that one's bestmove turns up.
static bool maybe_start_search(Session& session)
{
    Engine_Set& set = engines_of(session);
    if(session.searching!=Search_Kind::NONE)
    return false;
    Search_Kind kind = session.engine_to_move() && session.engine_error.empty() ? Search_Kind::PLAY
                     : session.watch_to_move() ? Search_Kind::WATCH
                     : session.analysis_wanted() ? Search_Kind::ANALYSIS
                     : Search_Kind::NONE;
    if(kind==Search_Kind::NONE)
    return false;
    int slot = kind!=Search_Kind::WATCH ? SLOT_SOLO
             : session.white_to_move()  ? SLOT_WHITE : SLOT_BLACK;
    Search_Request request;
    request.start_fen = session.root_fen();
    request.moves = session.moves();
    request.white_to_move = session.white_to_move();
    request.limits = kind==Search_Kind::ANALYSIS ? Go_Limits::analysis()
                    : session.clocked_now()       ? session.clock_go_limits(now_ms())
                    : kind==Search_Kind::PLAY     ? session.limits
                                                   : session.watch_limits_now();
    // A "ucinewgame" only when this engine has not seen this game before: in
    // Watch mode each side's process meets the game once, and in Analyse mode
    // moving around a game is not a reason to throw its table away.
    request.new_game = set.slot_game[slot]!=session.game_serial();
    long long token = link_of(set, slot).start_search(request);
    if(!token)
    return false;
    set.slot_game[slot] = session.game_serial();
    set.active = slot;
    set.awaiting = token;
    session.searching = kind;
    session.live_valid = false;
    return true;
}

// Plays whatever the engine answered, and tells the pages. Called from the
// poll loop, never from the worker thread.
static void collect_search(Session& session)
{
    Engine_Set& set = engines_of(session);
    if(set.active<0 || !set.links[set.active])
    return;
    Engine_Link& link = *set.links[set.active];
    Search_Info progress;
    if(session.searching!=Search_Kind::NONE && link.take_progress(progress))
    {
        if(session.analysing())
        session.set_analysis(progress);
        else
        {
            session.live = progress;
            session.live_valid = true;
        }
        broadcast(session);
    }

    Search_Result result;
    if(!link.take_result(result))
    return;
    if(result.token!=set.awaiting)
    return;                          // an aborted search's answer: not wanted any more
    // Checked here, with `session.searching` still what it was through the
    // whole search: clock_ticking() reads it (via watching()) to keep a Watch
    // clock counting through a Pause pressed mid-search, so ending the game
    // on time has to see the same thing collect_all()'s per-tick check does.
    if(session.check_flag(now_ms()))
    {
        set.active = -1;
        set.awaiting = 0;
        session.searching = Search_Kind::NONE;
        session.live_valid = false;
        session.watch_pause();
        broadcast(session);
        return;
    }
    Search_Kind kind = session.searching;
    set.active = -1;
    set.awaiting = 0;
    session.searching = Search_Kind::NONE;
    session.live_valid = false;
    if(!result.error.empty())
    {
        session.engine_error = result.error;
        session.watch_pause();       // a self-play game cannot go on without both engines
        session.clear_analysis();
        broadcast(session);
        return;
    }
    if(kind==Search_Kind::ANALYSIS)
    {
        // An analysis search we did not stop ourselves has simply run out of
        // depth. Its last line stays on the page; starting the same search
        // again would only spin, so the position is left as analysed.
        session.analysis_finished = true;
        broadcast(session);
        return;
    }
    bool mover_was_white = session.white_to_move();
    std::string error;
    if(!session.play(result.bestmove, error))
    {
        session.engine_error = "the engine answered " + result.bestmove + " — " + error;
        session.watch_pause();
        broadcast(session);
        return;
    }
    session.clock_credit_increment(mover_was_white);
    session.annotate_last(result.info, mover_was_white);
    if(kind==Search_Kind::WATCH)
    {
        // One step is one move: whatever it found, the game pauses here. A game
        // that just ended pauses too, so the button reads "Start" again.
        session.watch_step = false;
        std::string reason;
        if(session.result(reason)!=Outcome::ONGOING)
        session.watch_running = false;
    }
    broadcast(session);
}

// A closed tab must not leave engines thinking. Once no page has been on a
// session's stream for this long, its self-play game is paused, its analysis
// toggle goes off, and all its engine processes are let go; opening the page
// again starts them back up. The grace period is what keeps a page reload —
// which drops the stream for a moment — from counting as leaving.
constexpr long long ENGINE_IDLE_MS = 5000;

// Which engines the session's mode can still use. An engine this says nothing
// about is a process sitting on a transposition table for no one, so it goes:
// the Watch pair left behind by a switch back to Analyse is a few hundred MB
// of the room this repo deliberately keeps for running several engines at once.
static bool slot_wanted(const Session& session, int slot)
{
    if(session.mode==Mode::WATCH)
    return slot==SLOT_WHITE || slot==SLOT_BLACK;
    return slot==SLOT_SOLO;
}

static void release_idle_engines(const std::string& id, Session& session)
{
    Engine_Set& set = engines_of(session);
    if(server.subscribers(id)>0)
    set.alone_since = 0;
    else if(!set.alone_since)
    set.alone_since = now_ms();
    bool abandoned = set.alone_since && now_ms()-set.alone_since>=ENGINE_IDLE_MS;

    // Nobody is watching: stop whatever is thinking first. Its bestmove still
    // has to arrive before the worker is idle, so the processes themselves go
    // on a later tick, through the loop below.
    if(abandoned && (session.watch_running || session.watch_step || session.analysis_on || set.active>=0))
    {
        session.analysis_on = false;
        abort_search(session);
        return;
    }
    for(int slot=0;slot<N_SLOTS;slot++)
    if(set.links[slot] && (abandoned || !slot_wanted(session, slot)) && !set.links[slot]->searching_now())
    {
        set.links[slot].reset();     // ~Engine_Link joins its idle worker and quits the process
        set.slot_game[slot] = -1;    // a new process knows no game: it gets a "ucinewgame"
    }
}

// Every session with something in flight, once per poll iteration. The ids are
// copied out first: collect_search() may reach back into `engines`.
static void collect_all()
{
    std::vector<std::string> ids;
    for(auto& entry : engines)
    ids.push_back(entry.first);
    long long now = now_ms();
    bool ticking = false;
    for(const std::string& id : ids)
    {
        Session& session = sessions.get(id);
        // A flag can fall with nothing else happening — nobody moving, no
        // search finishing — so it is checked before anything else touches
        // this session this tick, not only when one of those does.
        if(session.check_flag(now))
        {
            abort_search(session);
            broadcast(session);
        }
        else
        {
            collect_search(session);
            if(maybe_start_search(session))
            broadcast(session);   // a search that had to wait for the last one
        }
        ticking = ticking || session.clock_ticking();
        release_idle_engines(id, session);
    }
    any_clock_ticking = ticking;
}

// The UCI binary Play mode drives: as given, else next to the working directory
// or beside gui/, so the server plays from the repo root and from gui/ alike.
static std::string find_engine(const std::string& option)
{
    if(!option.empty())
    return option;
    std::vector<std::string> candidates(1, std::string(ENGINE_PATH));
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe-1);
    if(n>0)
    {
        std::string dir = std::string(exe, n);
        dir = dir.substr(0, dir.rfind('/'));
        candidates.push_back(dir + "/../ascaniusfish_uci");
    }
    for(const std::string& path : candidates)
    if(access(path.c_str(), X_OK)==0)
    return path;
    return ENGINE_PATH;
}

// Where gui/web/ is: next to the binary, else relative to the working
// directory, so the server runs both from the repo root and from gui/. An
// explicit root= is used as given, so a wrong one is an error rather than a
// silent fallback to a different directory.
static std::string find_web_root(const std::string& option)
{
    if(!option.empty())
    return option;
    std::vector<std::string> candidates;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe-1);
    if(n>0)
    {
        std::string path(exe, n);
        candidates.push_back(path.substr(0, path.rfind('/')) + "/web");
    }
    candidates.push_back("gui/web");
    candidates.push_back("web");
    for(const std::string& dir : candidates)
    if(std::ifstream(dir + "/index.html").good())
    return dir;
    return "gui/web";
}

// Reads a file under web_root. Refuses anything that tries to leave it.
static bool read_web_file(const std::string& path, std::string& out)
{
    if(path.find("..")!=std::string::npos || path.find('\0')!=std::string::npos)
    return false;
    std::ifstream file(web_root + path, std::ios::binary);
    if(!file)
    return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

// A depth or clock setting parsed out of a request body: "kind" is "depth" or
// "clock", as both the Play and the Watch panel send it. `given` is false
// (everything else left default) when the body carries no "kind" at all — the
// caller then leaves whatever was already chosen alone.
struct Setting_Body
{
    bool given = false;
    bool clock = false;
    int depth = ENGINE_DEFAULT_DEPTH;
    long long base_ms = 0, inc_ms = 0;             // clock: White's, or the only side's (Watch)
    bool black_given = false;                      // Play's Custom only: an explicit override
    long long black_base_ms = 0, black_inc_ms = 0;
};

// "baseMs"/"incMs", or "blackBaseMs"/"blackIncMs" with `prefix` "black".
static bool read_clock_pair(const std::map<std::string, std::string>& body, const std::string& prefix,
                            long long& base_ms, long long& inc_ms, std::string& error)
{
    auto base = body.find(prefix + (prefix.empty() ? "baseMs" : "BaseMs"));
    auto inc = body.find(prefix + (prefix.empty() ? "incMs" : "IncMs"));
    base_ms = base==body.end() ? 0 : std::atoll(base->second.c_str());
    inc_ms = inc==body.end() ? 0 : std::atoll(inc->second.c_str());
    if(base_ms<MIN_CLOCK_BASE_MS || base_ms>MAX_CLOCK_BASE_MS)
    {
        error = "clock base must be " + std::to_string(MIN_CLOCK_BASE_MS) + " to "
              + std::to_string(MAX_CLOCK_BASE_MS) + " ms";
        return false;
    }
    if(inc_ms<0 || inc_ms>MAX_CLOCK_INC_MS)
    {
        error = "clock increment must be 0 to " + std::to_string(MAX_CLOCK_INC_MS) + " ms";
        return false;
    }
    return true;
}

static bool read_setting(const std::map<std::string, std::string>& body, Setting_Body& out, std::string& error)
{
    auto kind = body.find("kind");
    if(kind==body.end())
    return true;
    out.given = true;
    if(kind->second=="depth")
    {
        auto value = body.find("value");
        long long n = value==body.end() ? 0 : std::atoll(value->second.c_str());
        if(n<1 || n>MAX_DEPTH)
        {
            error = "depth must be 1 to " + std::to_string(MAX_DEPTH);
            return false;
        }
        out.clock = false;
        out.depth = (int)n;
        return true;
    }
    if(kind->second=="clock")
    {
        if(!read_clock_pair(body, "", out.base_ms, out.inc_ms, error))
        return false;
        out.clock = true;
        out.black_given = body.count("blackBaseMs")>0 || body.count("blackIncMs")>0;
        if(out.black_given && !read_clock_pair(body, "black", out.black_base_ms, out.black_inc_ms, error))
        return false;
        return true;
    }
    error = "kind must be depth or clock";
    return false;
}

// The session a request is about; "?id=" / "id" in the body, default "main".
static Session& session_of(const Request& req, const std::map<std::string, std::string>& body)
{
    auto it = body.find("id");
    return sessions.get(it!=body.end() ? it->second : req.param("id"));
}

static Response handle_get(const Request& req)
{
    if(req.path=="/api/state")
    return Response::json(sessions.get(req.param("id")).state_json());

    if(req.path=="/api/events")
    return Response::event_stream(sessions.get(req.param("id")).id);

    // The game as a file, so a browser can save it. The page already has the
    // same text in its state; this is for the URL.
    if(req.path=="/api/pgn")
    return Response::file(sessions.get(req.param("id")).pgn(), "text/plain; charset=utf-8");

    std::string path = req.path=="/" ? "/index.html" : req.path;
    std::string body;
    if(!read_web_file(path, body))
    return Response::text("not found: " + req.path, 404);
    return Response::file(body, mime_type(path));
}

static Response handle_post(const Request& req)
{
    std::map<std::string, std::string> body;
    if(!req.body.empty() && !json::parse_flat_object(req.body, body))
    return Response::json(json::error("request body is not a flat JSON object"), 400);

    Session& session = session_of(req, body);
    std::string error;
    bool ok = true;
    // Charges whatever ticked since the last sync to whoever is on the move
    // *before* this request's own handler below touches the tree or the mode —
    // several of them (nav, goto, delete, undo, fen) mutate first and only
    // call abort_search() after, so syncing there would attribute the elapsed
    // time to the position they just moved to rather than the one it was
    // actually spent on. /api/move and /api/watch's start/step sync again,
    // moments later, against the position/state their own handling settles
    // on — clock_sync() is incremental, so charging a negligible extra sliver
    // twice here is harmless.
    session.clock_sync(now_ms());

    if(req.path=="/api/move")
    {
        auto uci = body.find("uci");
        if(uci==body.end())
        return Response::json(json::error("no move given"), 400);
        // A move played into a running Play search would be answered for a
        // position the engine never saw. An analysis search is different: a
        // move is exactly how you tell it what to think about next.
        if(session.thinking())
        return Response::json(json::error("the engine is still thinking"), 409);
        // In Play mode a move of yours is also the answer to a failed search:
        // it clears the error, so the engine is asked again.
        session.engine_error.clear();
        if(session.check_flag(now_ms()))
        {
            abort_search(session);   // your clock had already run out
            broadcast(session);
            return Response::json(json::error("the game is over"), 409);
        }
        bool mover_white = session.white_to_move();
        ok = session.play(uci->second, error);
        if(ok)
        {
            session.clock_credit_increment(mover_white);
            abort_search(session);   // the analysis was about the position you just left
        }
    }
    else if(req.path=="/api/line")
    {
        // Walk the board along a line: the moves a click in the analysis line
        // covers, as a space-separated list of UCI moves.
        auto moves = body.find("moves");
        if(moves==body.end())
        return Response::json(json::error("no moves given"), 400);
        if(session.thinking())
        return Response::json(json::error("the engine is still thinking"), 409);
        ok = session.enter_line(moves->second, error);
        if(ok)
        abort_search(session);
    }
    else if(req.path=="/api/nav")
    {
        // The arrow keys and the buttons beside them. Moving the cursor moves
        // the position, so the analysis has to follow it: abort what was running
        // and the reply below starts a search on the position now shown.
        auto where = body.find("where");
        Nav parsed;
        if(where==body.end() || !nav_from_name(where->second, parsed))
        return Response::json(json::error("where must be back, forward, start, end, prev or next"), 400);
        if(session.thinking())
        return Response::json(json::error("the engine is still thinking"), 409);
        ok = session.navigate(parsed, error);
        if(ok)
        abort_search(session);
    }
    else if(req.path=="/api/goto")
    {
        // Clicking a move in the tree. The page sends the node's id, and an id
        // from before a deletion is refused rather than meaning another move.
        auto node = body.find("node");
        if(node==body.end())
        return Response::json(json::error("no node given"), 400);
        if(session.thinking())
        return Response::json(json::error("the engine is still thinking"), 409);
        ok = session.go_to(std::atoi(node->second.c_str()), error);
        if(ok)
        abort_search(session);
    }
    else if(req.path=="/api/promote")
    {
        // The line through the cursor becomes the main line. The position does
        // not change, so a running analysis is still about the right board and
        // is deliberately left alone.
        ok = session.promote(error);
    }
    else if(req.path=="/api/delete")
    {
        if(session.thinking())
        return Response::json(json::error("the engine is still thinking"), 409);
        ok = session.delete_variation(error);
        if(ok)
        abort_search(session);
    }
    else if(req.path=="/api/pgn")
    {
        auto pgn = body.find("pgn");
        if(pgn==body.end())
        return Response::json(json::error("no PGN given"), 400);
        abort_search(session);           // whatever it was thinking about is gone either way
        ok = session.load_pgn(pgn->second, error);
    }
    else if(req.path=="/api/analyse")
    {
        auto on = body.find("on");
        if(on==body.end() || (on->second!="true" && on->second!="false"))
        return Response::json(json::error("on must be true or false"), 400);
        bool wanted = on->second=="true";
        // Turning it off stops the analysis; turning it on clears whatever an
        // earlier one left, including the "ran out of depth" mark. A Play
        // search is none of this toggle's business and is left alone.
        if(!session.thinking())
        abort_search(session);
        session.analysis_on = wanted;
        if(wanted)
        session.engine_error.clear();
    }
    else if(req.path=="/api/fen")
    {
        auto fen = body.find("fen");
        if(fen==body.end())
        return Response::json(json::error("no FEN given"), 400);
        ok = session.set_fen(fen->second, error);
        if(ok)
        abort_search(session);   // a refused FEN leaves the engine thinking on
    }
    else if(req.path=="/api/reset")
    {
        abort_search(session);
        session.reset();
    }
    else if(req.path=="/api/undo")
    {
        ok = session.undo(error);
        if(ok)
        abort_search(session);   // whatever it was searching is about a position that is gone
    }
    else if(req.path=="/api/resign")
    {
        session.resign();
        abort_search(session);   // a finished game is nothing to analyse
    }
    else if(req.path=="/api/mode")
    {
        auto mode = body.find("mode");
        Mode parsed;
        if(mode==body.end() || !mode_from_name(mode->second, parsed))
        return Response::json(json::error("mode must be analyse, play or watch"), 400);
        // "Analyse this game", after a Play or Watch game ends, wants the tree
        // kept; a plain click on the mode selector wants a fresh board, the
        // same as switching into Play always does.
        auto keep = body.find("keepGame");
        bool keep_game = keep!=body.end() && keep->second=="true";
        abort_search(session);
        if(parsed==Mode::PLAY)
        session.start_play();
        else if(parsed==Mode::ANALYSE && !keep_game)
        session.start_analyse();
        else
        session.mode = parsed;
    }
    else if(req.path=="/api/play")
    {
        // The settings of a game against the engine. Applying them starts a new
        // game: a colour, a depth or a clock cannot sensibly change mid-game.
        Side_Choice side = session.side_choice;
        Go_Limits limits = session.limits;
        bool clock_on = session.play_clock_on;
        long long base_ms[2] = {session.play_base_ms[0], session.play_base_ms[1]};
        long long inc_ms[2] = {session.play_inc_ms[0], session.play_inc_ms[1]};
        auto given = body.find("side");
        if(given!=body.end() && !side_choice_from_name(given->second, side))
        return Response::json(json::error("side must be white, black or random"), 400);
        Setting_Body setting;
        if(!read_setting(body, setting, error))
        return Response::json(json::error(error), 400);
        if(setting.given)
        {
            clock_on = setting.clock;
            if(setting.clock)
            {
                base_ms[0] = base_ms[1] = setting.base_ms;
                inc_ms[0] = inc_ms[1] = setting.inc_ms;
                if(setting.black_given)
                {
                    base_ms[1] = setting.black_base_ms;
                    inc_ms[1] = setting.black_inc_ms;
                }
            }
            else
            limits.depth = setting.depth;
        }
        abort_search(session);
        session.side_choice = side;
        session.limits = limits;
        session.play_clock_on = clock_on;
        session.play_base_ms[0] = base_ms[0];
        session.play_base_ms[1] = base_ms[1];
        session.play_inc_ms[0] = inc_ms[0];
        session.play_inc_ms[1] = inc_ms[1];
        session.start_play();
    }
    else if(req.path=="/api/watch")
    {
        // Ascaniusfish against itself. One request carries both a side's
        // setting ("side" plus "kind"/"value" or the clock fields) and a run
        // control ("action"). Unlike a side's setting alone, a Clock/Fixed-
        // depth or preset/Custom choice restarts the game, matching Play — the
        // settings selector is hidden once a game is on for exactly this
        // reason (a preset click sends this twice, once per side).
        auto side = body.find("side");
        bool restart = false;
        if(side!=body.end())
        {
            if(side->second!="white" && side->second!="black")
            return Response::json(json::error("side must be white or black"), 400);
            int i = side->second=="white" ? 0 : 1;
            Setting_Body setting;
            if(!read_setting(body, setting, error))
            return Response::json(json::error(error), 400);
            if(setting.given)
            {
                restart = true;
                session.watch_clock_on = setting.clock;
                if(setting.clock)
                {
                    session.watch_base_ms[i] = setting.base_ms;
                    session.watch_inc_ms[i] = setting.inc_ms;
                }
                else
                session.watch_limits[i].depth = setting.depth;
            }
        }
        if(restart)
        {
            abort_search(session);
            session.start_watch();
        }
        auto action = body.find("action");
        if(action!=body.end())
        {
            if(session.mode!=Mode::WATCH)
            return Response::json(json::error("the board is not in Watch mode"), 409);
            if(action->second=="pause")
            // Deliberately not an abort: the move being thought about is
            // finished and played, which is what "after the current move" means.
            session.watch_pause();
            else if(action->second=="start" || action->second=="step")
            {
                std::string reason;
                if(session.result(reason)!=Outcome::ONGOING)
                return Response::json(json::error("the game is over — start a new one"), 409);
                if(!session.at_tip())
                return Response::json(json::error("go to the end of the line first"), 409);
                session.engine_error.clear();
                session.watch_running = action->second=="start";
                session.watch_step = action->second=="step";
                session.clock_sync(now_ms());   // arm the resuming mover's clock from this press
            }
            else
            return Response::json(json::error("action must be start, pause or step"), 400);
        }
    }
    else if(req.path=="/api/flip")
    session.flipped = !session.flipped;
    else if(req.path=="/api/settings")
    {
        // The gear's switches. Each is only applied when the body carries it, so
        // one switch can be flipped without saying anything about the other.
        static const struct { const char* name; bool Session::*field; } switches[] =
        {
            { "evalBar",    &Session::show_eval_bar    },
            { "engineLine", &Session::show_engine_line },
        };
        for(const auto& option : switches)
        {
            auto given = body.find(option.name);
            if(given==body.end())
            continue;
            if(given->second!="true" && given->second!="false")
            return Response::json(json::error(std::string(option.name) + " must be true or false"), 400);
            session.*option.field = given->second=="true";
        }
    }
    else
    return Response::text("not found: " + req.path, 404);

    if(!ok)
    return Response::json(json::error(error), 400);
    // A move the human just made, or a fresh game the engine has white in:
    // ask for its reply before answering, so the page sees "thinking" at once.
    maybe_start_search(session);
    broadcast(session);                               // every other open page follows along
    return Response::json(session.state_json());
}

// Ctrl-C. Every search is asked to stop, and once the workers are idle the
// links go, which sends each engine a "quit" and reaps it. A worker still
// waiting for a bestmove would make ~Engine_Link block — "go infinite" waits a
// day — so after the grace period the exit itself does the job instead: when
// this process goes, its engines read EOF on stdin and stop.
static void quit_engines()
{
    for(auto& entry : engines)
    for(std::unique_ptr<Engine_Link>& link : entry.second->links)
    if(link)
    link->abort();
    for(int waited=0;waited<100;waited++)
    {
        bool busy = false;
        for(auto& entry : engines)
        for(std::unique_ptr<Engine_Link>& link : entry.second->links)
        busy = busy || (link && link->searching_now());
        if(!busy)
        {
            engines.clear();
            return;
        }
        usleep(20000);
    }
}

static void request_stop(int) { server.stopping = 1; }

int main(int argc, char** argv)
{
    // Writing to a dead engine's pipe must be an error, not the end of the
    // server: an engine that crashed is something the page gets told about.
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);

    // The engine's attack tables. Without them sliding-piece attacks are
    // garbage, which shows up as in_check() missing checks rather than as a
    // crash — every tool that touches movegen starts with these four lines.
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);  // bishop
    init_sliders_attacks(0);  // rook

    int port = DEFAULT_PORT;
    std::string root_option, start_fen, engine_option;
    for(int i=1;i<argc;i++)
    {
        std::string arg = argv[i];
        size_t eq = arg.find('=');
        if(eq==std::string::npos)
        {
            std::fprintf(stderr, "gui: unrecognised argument \"%s\" (expected key=value)\n", argv[i]);
            return 2;
        }
        std::string key = arg.substr(0, eq), value = arg.substr(eq+1);
        if(key=="port")      port = std::atoi(value.c_str());
        else if(key=="root") root_option = value;
        else if(key=="fen")  start_fen = value;
        else if(key=="engine") engine_option = value;
        else
        {
            std::fprintf(stderr, "gui: unknown option \"%s\"\n", key.c_str());
            return 2;
        }
    }

    engine_path = find_engine(engine_option);
    web_root = find_web_root(root_option);
    if(!std::ifstream(web_root + "/index.html").good())
    {
        std::fprintf(stderr, "gui: no index.html under \"%s\" (run from the repo root, or pass root=<dir>)\n", web_root.c_str());
        return 2;
    }

    Session& main_session = sessions.get("main");
    if(!start_fen.empty())
    {
        std::string error;
        if(!main_session.set_fen(start_fen, error))
        {
            std::fprintf(stderr, "gui: fen= %s\n", error.c_str());
            return 2;
        }
    }

    server.handler = [](const Request& req) -> Response
    {
        if(req.method=="GET")
        return handle_get(req);
        if(req.method=="POST")
        return handle_post(req);
        if(req.method=="HEAD" || req.method=="OPTIONS")
        return Response::text("");
        return Response::text("method not allowed: " + req.method, 405);
    };
    // A page that just connected has no position yet; send it one.
    server.on_subscribe = [](const std::string& topic) { broadcast(sessions.get(topic)); };
    // Where a finished search becomes a move on the board: on this thread, so
    // nothing the worker produced ever touches a socket itself.
    server.on_tick = collect_all;
    // A running clock is checked for a flag fall often; an idle server sits
    // at the old fixed interval and costs nothing.
    server.poll_timeout = [] { return any_clock_ticking ? CLOCK_POLL_MS : IDLE_POLL_MS; };

    if(!server.listen_on(port))
    {
        std::fprintf(stderr, "gui: could not bind a port from %d upwards\n", port);
        return 2;
    }

    std::printf("Ascaniusfish GUI on http://localhost:%d  (serving %s, Ctrl-C to stop)\n",
                server.port(), web_root.c_str());
    std::fflush(stdout);
    server.run();
    std::printf("\nStopping.\n");
    quit_engines();
    std::fflush(nullptr);
    std::_Exit(0);   // anything still thinking is left to EOF on its stdin
}
