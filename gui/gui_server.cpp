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
//
// This is the shell the later GUI issues plug into: Play and Watch appear in the
// mode selector but do nothing yet, and the SSE stream is already how every open
// page learns about a new position.
#include "http_server.hpp"
#include "session.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

constexpr int DEFAULT_PORT = 8173;

static Sessions sessions;
static Http_Server server;
static std::string web_root;

// Pushes the current position to every page watching this session.
static void broadcast(Session& session)
{
    server.publish(session.id, "state", session.state_json());
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
        ok = session.play(uci->second, error);
    }
    else if(req.path=="/api/fen")
    {
        auto fen = body.find("fen");
        if(fen==body.end())
        return Response::json(json::error("no FEN given"), 400);
        ok = session.set_fen(fen->second, error);
    }
    else if(req.path=="/api/reset")
    session.reset();
    else if(req.path=="/api/undo")
    ok = session.undo(error);
    else if(req.path=="/api/mode")
    {
        auto mode = body.find("mode");
        Mode parsed;
        if(mode==body.end() || !mode_from_name(mode->second, parsed))
        return Response::json(json::error("mode must be analyse, play or watch"), 400);
        session.mode = parsed;
    }
    else if(req.path=="/api/flip")
    session.flipped = !session.flipped;
    else
    return Response::text("not found: " + req.path, 404);

    if(!ok)
    return Response::json(json::error(error), 400);
    broadcast(session);                               // every other open page follows along
    return Response::json(session.state_json());
}

int main(int argc, char** argv)
{
    // The engine's attack tables. Without them sliding-piece attacks are
    // garbage, which shows up as in_check() missing checks rather than as a
    // crash — every tool that touches movegen starts with these four lines.
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);  // bishop
    init_sliders_attacks(0);  // rook

    int port = DEFAULT_PORT;
    std::string root_option, start_fen;
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
        else
        {
            std::fprintf(stderr, "gui: unknown option \"%s\"\n", key.c_str());
            return 2;
        }
    }

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
