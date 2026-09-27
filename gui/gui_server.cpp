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
// Play and Analyse mode make this a UCI client of ./ascaniusfish_uci
// (gui/engine_link.hpp): the search runs on a worker thread, so a request never
// waits for it and the SSE stream carries the thinking indicator, the engine's
// move and — in Analyse mode — every iteration of a "go infinite" search on the
// position now on the board. Watch mode is still selector-only.
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
constexpr long long MAX_MOVETIME_MS = 600000;
constexpr int MAX_DEPTH = 40;

static Sessions sessions;
static Http_Server server;
static std::string web_root;
static std::string engine_path = ENGINE_PATH;

// One engine process per session, started on the first search it is asked for,
// plus the token of the search that session is still waiting for — an aborted
// search's bestmove arrives all the same and is dropped by its token.
struct Engine_Slot
{
    Engine_Link link;
    long long awaiting = 0;
};

static std::map<std::string, std::unique_ptr<Engine_Slot>> engines;

static Engine_Slot& engine_of(const Session& session)
{
    std::unique_ptr<Engine_Slot>& slot = engines[session.id];
    if(!slot)
    {
        slot.reset(new Engine_Slot);
        slot->link.path = engine_path;
        slot->link.on_update = [] { server.wake(); };   // the only cross-thread call there is
    }
    return *slot;
}

// Pushes the current position to every page watching this session.
static void broadcast(Session& session)
{
    server.publish(session.id, "state", session.state_json());
}

// Stops a running search and forgets its answer, for anything that changes the
// position under the engine: a new game, a FEN, a take back, a mode switch.
static void abort_search(Session& session)
{
    Engine_Slot& slot = engine_of(session);
    slot.link.abort();
    slot.awaiting = 0;
    session.searching = Search_Kind::NONE;
    session.live_valid = false;
    session.clear_analysis();
}

// Asks the engine for whatever this session now wants thought about: its move
// in Play mode, the position on the board in Analyse mode. Returns at once; the
// answer arrives through on_tick. False when nothing is wanted, or when the
// engine is still finishing an aborted search — on_tick tries again as soon as
// that one's bestmove turns up.
static bool maybe_start_search(Session& session)
{
    Engine_Slot& slot = engine_of(session);
    if(session.searching!=Search_Kind::NONE)
    return false;
    Search_Kind kind = session.engine_to_move() && session.engine_error.empty() ? Search_Kind::PLAY
                     : session.analysis_wanted() ? Search_Kind::ANALYSIS
                     : Search_Kind::NONE;
    if(kind==Search_Kind::NONE)
    return false;
    Search_Request request;
    request.start_fen = session.root_fen();
    request.moves = session.moves();
    request.limits = kind==Search_Kind::PLAY ? session.limits : Go_Limits::analysis();
    request.new_game = session.moves().empty();
    long long token = slot.link.start_search(request);
    if(!token)
    return false;
    slot.awaiting = token;
    session.searching = kind;
    session.live_valid = false;
    return true;
}

// Plays whatever the engine answered, and tells the pages. Called from the
// poll loop, never from the worker thread.
static void collect_search(Session& session)
{
    Engine_Slot& slot = engine_of(session);
    Search_Info progress;
    if(session.searching!=Search_Kind::NONE && slot.link.take_progress(progress))
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
    if(!slot.link.take_result(result))
    return;
    if(result.token!=slot.awaiting)
    return;                          // an aborted search's answer: not wanted any more
    Search_Kind kind = session.searching;
    slot.awaiting = 0;
    session.searching = Search_Kind::NONE;
    session.live_valid = false;
    if(!result.error.empty())
    {
        session.engine_error = result.error;
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
    bool engine_is_white = !session.human_white;
    std::string error;
    if(!session.play(result.bestmove, error))
    session.engine_error = "the engine answered " + result.bestmove + " — " + error;
    else
    session.annotate_last(result.info, engine_is_white);
    broadcast(session);
}

// Every session with something in flight, once per poll iteration. The ids are
// copied out first: collect_search() may reach back into `engines`.
static void collect_all()
{
    std::vector<std::string> ids;
    for(auto& entry : engines)
    ids.push_back(entry.first);
    for(const std::string& id : ids)
    {
        Session& session = sessions.get(id);
        collect_search(session);
        if(maybe_start_search(session))
        broadcast(session);   // a search that had to wait for the last one
    }
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
        ok = session.play(uci->second, error);
        if(ok)
        abort_search(session);   // the analysis was about the position you just left
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
        abort_search(session);
        session.mode = parsed;
        if(parsed==Mode::PLAY)
        session.start_play();
    }
    else if(req.path=="/api/play")
    {
        // The settings of a game against the engine. Applying them starts a new
        // game: a colour or a search limit cannot sensibly change mid-game.
        Side_Choice side = session.side_choice;
        Go_Limits limits = session.limits;
        auto given = body.find("side");
        if(given!=body.end() && !side_choice_from_name(given->second, side))
        return Response::json(json::error("side must be white, black or random"), 400);
        given = body.find("kind");
        if(given!=body.end())
        {
            auto value = body.find("value");
            long long n = value==body.end() ? 0 : std::atoll(value->second.c_str());
            if(given->second=="depth")
            {
                if(n<1 || n>MAX_DEPTH)
                return Response::json(json::error("depth must be 1 to " + std::to_string(MAX_DEPTH)), 400);
                limits = Go_Limits();
                limits.depth = (int)n;
            }
            else if(given->second=="movetime")
            {
                if(n<1 || n>MAX_MOVETIME_MS)
                return Response::json(json::error("move time must be 1 to " + std::to_string(MAX_MOVETIME_MS) + " ms"), 400);
                limits = Go_Limits();
                limits.depth = 0;
                limits.movetime_ms = n;
            }
            else
            return Response::json(json::error("kind must be depth or movetime"), 400);
        }
        abort_search(session);
        session.side_choice = side;
        session.limits = limits;
        session.start_play();
    }
    else if(req.path=="/api/flip")
    session.flipped = !session.flipped;
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

int main(int argc, char** argv)
{
    // Writing to a dead engine's pipe must be an error, not the end of the
    // server: an engine that crashed is something the page gets told about.
    std::signal(SIGPIPE, SIG_IGN);

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

    if(!server.listen_on(port))
    {
        std::fprintf(stderr, "gui: could not bind a port from %d upwards\n", port);
        return 2;
    }

    std::printf("Ascaniusfish GUI on http://localhost:%d  (serving %s, Ctrl-C to stop)\n",
                server.port(), web_root.c_str());
    std::fflush(stdout);
    server.run();
    return 0;
}
