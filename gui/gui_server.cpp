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
//   port=8173         first port to try; the next 20 are tried if it is taken
//   bind=127.0.0.1    address to listen on; 0.0.0.0 reaches the LAN (issue #27)
//   root=gui/web      directory the static files come from
//   fen=<fen>         starting position of the "main" session
//   engine=...        the UCI binary Play mode drives (default ./ascaniusfish_uci)
//   syzygy=<dir>      Syzygy tables: the page shows a tablebase position's exact
//                     result, and the engines are given the path (issue #40)
//   gaviota=<dir>     Gaviota DTM tables, on top of syzygy=: the mate in N and
//                     the mating line, also given to the engines (issue #77)
//   ptt=<file>        where long searches are kept (issue #79; default
//                     ~/.ascaniusfish/ptt.txt, shared by every worktree), none = off
//   tunnel=cloudflared  also launch `cloudflared tunnel --url` at this port and
//                     print the combined shareable link once it comes up
//   max_connections=64, max_engines=2, max_boards=20
//                     the load limits (#109); also GUI_MAX_CONNECTIONS,
//                     GUI_MAX_ENGINES, GUI_MAX_BOARDS, an option winning
//   board_idle_ms=86400000  how long a board may sit with no page open before
//                     it is removed (#112); shrunk by the diagnostic, not
//                     meant to be changed otherwise
//   empty_board_idle_ms=60000  the same for a board still at the start
//                     position with no moves (#111), likewise
//
// A token, new every run and printed once below, gates every route including
// static files and /api/events: without it (as a "token=" cookie, set once a
// request's "?token=" query matches, or that query itself) the answer is 401
// before a session or an engine is ever touched. That is what makes bind=
// anything but 127.0.0.1 — a LAN, or a tunnel's public port — safe to use;
// see docs/REMOTE_PLAY.md for sharing a game over one with no hosting cost.
//
// On the internet, behind a reverse proxy (#109), GUI_PASSWORD replaces the
// token with one shared password: a login page, then a signed cookie good for
// 30 days (gui/access.hpp). GUI_COOKIE_SECRET signs it and must be set with it;
// GUI_TRUST_PROXY=1 takes the client's address and https from the proxy's
// headers; GUI_LOGIN_FAILS_PER_IP (10) and GUI_LOGIN_FAILS_TOTAL (100) are the
// failed logins allowed in 5 minutes. docs/GUI.md has the whole setup.
//
// Play, Analyse and Watch mode make this a UCI client of ./ascaniusfish_uci
// (gui/engine_link.hpp): the search runs on a worker thread, so a request never
// waits for it and the SSE stream carries the thinking indicator, the engine's
// move and — in Analyse mode — every iteration of a "go infinite" search on the
// position now on the board. Watch mode drives one of those processes for both
// sides (Pure watch, #91) or, in tournament mode, two, one per side, and plays
// them against each other; when the last page watching a
// session goes away its game is paused and its engines are let go, so a closed
// tab never leaves two searches running for nobody. Correspondence mode (#101)
// runs one more, the root engine, on a position of the user's choosing beside
// the Analyse engine.
#include "access.hpp"
#include "http_server.hpp"
#include "session.hpp"
#include "../lib/engine_version.hpp"

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <ctime>
#include <sys/wait.h>
#include <thread>

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

// ---------------------------------------------------------------- access token (#27)
//
// New every run, printed once at startup and never logged with a request
// afterwards. A request authenticates with a "token=" cookie, or (only to
// obtain that cookie) a "?token=" query parameter — needed because EventSource
// and a plain <script src> cannot carry a custom header. handle_get/handle_post
// check it before doing anything else, so an unauthenticated request never
// reaches session_of() and never creates a session or starts an engine.
static std::string gui_token;

static std::string generate_token()
{
    unsigned char raw[24];
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if(urandom)
    urandom.read((char*)raw, sizeof raw);
    if(!urandom)
    {
        std::random_device rd;   // /dev/urandom missing: still not attacker-predictable
        for(unsigned char& b : raw)
        b = (unsigned char)rd();
    }
    static const char* hex = "0123456789abcdef";
    std::string out(sizeof raw*2, '0');
    for(size_t i=0;i<sizeof raw;i++)
    {
        out[2*i]   = hex[raw[i]>>4];
        out[2*i+1] = hex[raw[i]&0xF];
    }
    return out;
}

// Constant-time-ish compare: this is a home server, not a bank, but a token
// comparison is exactly the kind of thing that costs nothing to do properly.
static bool tokens_equal(const std::string& a, const std::string& b)
{
    if(a.empty() || a.size()!=b.size())
    return false;
    unsigned char diff = 0;
    for(size_t i=0;i<a.size();i++)
    diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return diff==0;
}

struct Auth
{
    bool ok = false;
    bool set_cookie = false;  // authenticated via the query token: hand back a cookie
};

// ---------------------------------------------------------------- password login (#109)
//
// With GUI_PASSWORD set the token above is off: the login cookie is the only
// way in, "?token=" opens nothing, and no token is printed. /login and
// /logout are the only routes reached without the cookie.
static gui_access::Login login;
static gui_access::Login_Limiter login_limiter;
static bool trust_proxy = false;

static bool logged_in(const Request& req)
{
    auto cookie = req.headers.find("cookie");
    return cookie!=req.headers.end()
        && login.cookie_ok(gui_access::cookie_value(cookie->second, gui_access::COOKIE_NAME), time(nullptr));
}

static Auth check_auth(const Request& req)
{
    if(login.on())
    return {logged_in(req), false};
    auto cookie = req.headers.find("cookie");
    if(cookie!=req.headers.end() && tokens_equal(gui_access::cookie_value(cookie->second, "token"), gui_token))
    return {true, false};
    if(tokens_equal(req.param("token"), gui_token))
    return {true, true};
    return {false, false};
}

// A fresh login cookie, or (clear) one that removes it. Secure when the
// proxy says the browser came over https.
static std::string login_cookie(const Request& req, bool clear)
{
    std::string c = std::string(gui_access::COOKIE_NAME) + "=" + (clear ? "" : login.make_cookie(time(nullptr)))
                  + "; Path=/; Max-Age=" + (clear ? "0" : std::to_string(gui_access::COOKIE_SECONDS))
                  + "; HttpOnly; SameSite=Lax";
    if(gui_access::client_of(req.peer, req.headers, trust_proxy).https)
    c += "; Secure";
    return c;
}

static Response redirect(const std::string& to, const std::string& cookie = "")
{
    Response r = Response::text("", 303);
    r.location = to;
    r.set_cookie = cookie;
    return r;
}

static bool read_web_file(const std::string& path, std::string& out);

static void replace_all(std::string& s, const std::string& from, const std::string& to)
{
    for(size_t at = s.find(from); at!=std::string::npos; at = s.find(from, at+to.size()))
    s.replace(at, from.size(), to);
}

// gui/web/login.html with its {{message}} and {{next}} filled in. Only the
// server's own messages go into it, and `next` is escaped.
static Response login_page(const std::string& message, const std::string& next, int status)
{
    std::string page;
    if(!read_web_file("/login.html", page))
    page = "<!doctype html><title>Log in</title><p>{{message}}<form method=\"post\" action=\"/login\">"
           "<input type=\"hidden\" name=\"next\" value=\"{{next}}\"><input type=\"password\" name=\"password\" autofocus>"
           "<button>Log in</button></form>";
    replace_all(page, "{{message}}", gui_access::html_escape(message));
    replace_all(page, "{{next}}", gui_access::html_escape(next));
    Response r = Response::file(page, "text/html; charset=utf-8");
    r.status = status;
    return r;
}

static std::string minutes_text(long long ms)
{
    long long m = (ms+59999)/60000;
    return m<=1 ? "a minute" : std::to_string(m) + " minutes";
}

// GET and POST /login, POST /logout. False for any other route.
static bool handle_login_routes(const Request& req, Response& res)
{
    if(req.path=="/login" && req.method=="GET")
    {
        std::string next = gui_access::safe_next(req.param("next"));
        res = logged_in(req) ? redirect(next) : login_page("", next, 200);
        return true;
    }
    if(req.path=="/login" && req.method=="POST")
    {
        std::string next = gui_access::safe_next(Request::field(req.body, "next"));
        std::string ip = gui_access::client_of(req.peer, req.headers, trust_proxy).ip;
        long long now = now_ms();
        bool whole_server = false;
        long long wait = login_limiter.wait_ms(ip, now, whole_server);
        if(wait>0)
        res = login_page(std::string(whole_server ? "Too many failed logins on this server" : "Too many failed logins from your address")
                         + ". Try again in " + minutes_text(wait) + ".", next, 429);
        else if(!login.password_ok(Request::field(req.body, "password")))
        {
            login_limiter.failed(ip, now);
            res = login_page("Wrong password.", next, 401);
        }
        else
        res = redirect(next, login_cookie(req, false));
        return true;
    }
    if(req.path=="/logout" && req.method=="POST")
    {
        res = redirect("/login", login_cookie(req, true));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- load limits (#109)
//
// Engine processes and boards, on top of the connection cap in http_server.hpp.
// Each refusal is a message the page shows, never a crash.
static int max_engines = 2;
static size_t max_boards = 20;

// A request for a board that does not exist yet, with max_boards there
// already, is refused before anything makes it.
static bool board_refused(const std::string& raw_id, Response& res)
{
    if(sessions.has(raw_id) || sessions.size()<max_boards)
    return false;
    res = Response::json(json::error("this server already has its maximum of " + std::to_string(max_boards)
                                     + " boards; open the link of an existing one"), 503);
    return true;
}

static std::string auth_cookie()
{
    return "token=" + gui_token + "; Path=/; HttpOnly; SameSite=Lax";
}

// -------------------------------------------------------------- cloudflared tunnel (#27)
//
// tunnel=cloudflared launches `cloudflared tunnel --url http://localhost:<port>`
// as a child process, its own stdout+stderr redirected to a log file (a pipe
// would risk cloudflared blocking on a full one hours into a quiet session,
// since nothing here keeps reading it after startup) so its trycloudflare.com
// URL can be scraped out and combined with the token into one link.
static pid_t tunnel_pid = -1;
static std::string tunnel_log_path;

// The first "https://...trycloudflare.com..." word in cloudflared's log text.
static std::string extract_tunnel_url(const std::string& text)
{
    for(size_t pos = text.find("https://"); pos!=std::string::npos; pos = text.find("https://", pos+1))
    {
        size_t end = pos;
        while(end<text.size() && !std::isspace((unsigned char)text[end]))
        end++;
        std::string candidate = text.substr(pos, end-pos);
        if(candidate.find("trycloudflare.com")!=std::string::npos)
        return candidate;
    }
    return "";
}

// Forks cloudflared and waits (polling its log file, not the main event loop —
// this runs once at startup, before server.run()) up to timeout_ms for its
// URL. Empty with `error` set on a bad fork, a missing cloudflared binary, an
// early exit, or a timeout; the local link still works in every one of those.
static std::string start_tunnel_and_wait(int port, long long timeout_ms, std::string& error)
{
    tunnel_log_path = "/tmp/ascaniusfish_gui_tunnel_" + std::to_string(getpid()) + ".log";
    int fd = open(tunnel_log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if(fd<0)
    {
        error = "could not create " + tunnel_log_path + " for cloudflared's output";
        tunnel_log_path.clear();
        return "";
    }
    pid_t pid = fork();
    if(pid<0)
    {
        error = "fork failed";
        close(fd);
        return "";
    }
    if(pid==0)
    {
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        std::string local_url = "http://localhost:" + std::to_string(port);
        execlp("cloudflared", "cloudflared", "tunnel", "--url", local_url.c_str(), (char*)nullptr);
        _exit(127);   // execlp only returns on failure
    }
    close(fd);
    tunnel_pid = pid;

    long long deadline = now_ms()+timeout_ms;
    for(;;)
    {
        std::ifstream log(tunnel_log_path);
        std::ostringstream buffer;
        buffer << log.rdbuf();
        std::string url = extract_tunnel_url(buffer.str());
        if(!url.empty())
        return url;
        int status;
        if(waitpid(pid, &status, WNOHANG)==pid)
        {
            tunnel_pid = -1;
            error = WIFEXITED(status) && WEXITSTATUS(status)==127
                  ? "cloudflared not found on PATH — install it first (see docs/REMOTE_PLAY.md)"
                  : "cloudflared exited before printing a tunnel URL (see " + tunnel_log_path + ")";
            return "";
        }
        if(now_ms()>=deadline)
        {
            error = "still waiting on cloudflared after " + std::to_string(timeout_ms/1000)
                  + "s — it may yet come up; see " + tunnel_log_path;
            return "";
        }
        usleep(150000);
    }
}

// The HTTP status `url` answers with right now, or 0 if curl couldn't even
// connect (DNS failure, connection refused, or its own --max-time). While a
// quick tunnel hasn't finished routing, Cloudflare's edge answers on its own
// with an error page (its own 502/530, not "no response"), so this checks for
// an actual 2xx rather than just "curl got something back".
static bool url_answers_ok(const std::string& url, int timeout_s)
{
    std::string cmd = "curl -s -o /dev/null -w '%{http_code}' --max-time "
                     + std::to_string(timeout_s) + " '" + url + "'";
    FILE* p = popen(cmd.c_str(), "r");
    if(!p) return false;
    char buf[16] = {0};
    if(!std::fgets(buf, sizeof buf, p)) buf[0] = '\0';
    pclose(p);
    int code = std::atoi(buf);
    return code>=200 && code<400;
}

// cloudflared prints the trycloudflare.com URL as soon as it registers with
// Cloudflare's edge, but Cloudflare's own message alongside it warns the
// hostname "may take some time to be reachable" — that propagation delay is
// on Cloudflare's side and can run well past what start_tunnel_and_wait()
// waits for, which is what made the printed "Shareable link" look broken for
// minutes at a time with nothing to show it was still just pending. This
// polls the real link in the background — never
// blocking startup, since the local link already works while it does — and
// prints a confirmation once it actually answers, or a note if it still
// hasn't after a long wait.
static void confirm_tunnel_reachable(std::string share_url)
{
    long long deadline = now_ms() + 5*60*1000;
    while(now_ms()<deadline)
    {
        if(tunnel_pid<0) return;   // stopped (Ctrl-C, or cloudflared died) before it came up
        if(url_answers_ok(share_url, 5))
        {
            std::printf("Tunnel confirmed reachable — the shareable link is live.\n");
            std::fflush(stdout);
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    std::fprintf(stderr,
        "gui: tunnel: still not reachable after 5 minutes — quick tunnels can be this slow to "
        "route, or may not come up at all; wait longer, restart, or see docs/REMOTE_PLAY.md for "
        "a named tunnel instead.\n");
    std::fflush(stderr);
}

// Ctrl-C must take the tunnel down too, or it keeps forwarding to a server
// that is no longer there.
static void stop_tunnel()
{
    if(tunnel_pid>0)
    {
        kill(tunnel_pid, SIGTERM);
        int status;
        waitpid(tunnel_pid, &status, 0);
        tunnel_pid = -1;
    }
    if(!tunnel_log_path.empty())
    unlink(tunnel_log_path.c_str());
}

// A session's engine processes. Play and Analyse share one; Watch has one per
// side in tournament mode, so the two self-play engines keep their own
// transposition tables, and in Pure watch (#91) SLOT_WHITE plays both sides,
// so each search starts from the table the other side's just filled. Neither
// is also the analysis engine. Each starts on the first search it is asked
// for, so a session that never watches never forks the extra processes.
// Correspondence mode (#101) adds SLOT_ROOT, the root engine, which searches
// beside the solo one (the Searcher) rather than in turn with it, so it has a
// token of its own and is not the session's one search (`active`).
enum { SLOT_SOLO = 0, SLOT_WHITE = 1, SLOT_BLACK = 2, SLOT_ROOT = 3, N_SLOTS = 4 };

static int watch_slot(const Session& session)
{
    return session.watch_pure || session.white_to_move() ? SLOT_WHITE : SLOT_BLACK;
}

struct Engine_Set
{
    std::unique_ptr<Engine_Link> links[N_SLOTS];
    int active = -1;                         // the slot whose search is running
    long long awaiting = 0;                  // its token; an aborted search's answer is dropped
    int slot_game[N_SLOTS] = {-1, -1, -1, -1};   // the game each slot last searched in (SLOT_ROOT: the root's corr_serial)
    long long alone_since = 0;               // when the last page left; 0 while one is watching
    long long root_awaiting = 0;             // the root search's token (#101); 0 = none out
};

constexpr long long ENGINE_IDLE_MS = 5000;   // see release_idle_engines()

static bool abandoned(const Engine_Set& set)
{
    return set.alone_since && now_ms()-set.alone_since>=ENGINE_IDLE_MS;
}

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

// Lets go of the engines a board's mode no longer uses (all of them once
// `gone`), unless one is still finishing a search.
static bool slot_wanted(const Session& session, int slot);

static void drop_unused_links(const Session& session, Engine_Set& set, bool gone)
{
    for(int slot=0;slot<N_SLOTS;slot++)
    if(set.links[slot] && (gone || !slot_wanted(session, slot)) && !set.links[slot]->searching_now())
    {
        set.links[slot].reset();     // ~Engine_Link joins its idle worker and quits the process
        set.slot_game[slot] = -1;    // a new process knows no game: it gets a "ucinewgame"
    }
}

static int live_engines()
{
    int n = 0;
    for(auto& entry : engines)
    for(const std::unique_ptr<Engine_Link>& link : entry.second->links)
    n += link!=nullptr;
    return n;
}

// Whether `slot` of this set may search: it has its process already, or one
// more fits under max_engines (#109). A full server first lets go of what
// nobody uses any more — the engine of a mode just left, or of a board whose
// pages are gone — which release_idle_engines() would do on the next tick.
static bool engine_room(Engine_Set& set, int slot)
{
    if(set.links[slot] || live_engines()<max_engines)
    return true;
    for(auto& entry : engines)
    drop_unused_links(sessions.get(entry.first), *entry.second, entry.second.get()!=&set && abandoned(*entry.second));
    return live_engines()<max_engines;
}

static std::string engines_full()
{
    return "all " + std::to_string(max_engines) + " engines of this server are in use on other boards;"
           " try again once one of them is closed";
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

// ------------------------------------------------------------- board deletion and expiry (#112)
//
// A board nobody has open is removed after 24 h (one still untouched after a
// minute, #111), the same "watched or not"
// signal release_idle_engines() reads (server.subscribers()), just on a
// day's clock rather than a few seconds'. The explicit "Delete board" button
// lands here too. Either way the board cannot be torn down on the spot if
// its engine is mid-search: ~Engine_Link would block joining the worker (see
// quit_engines()), so deletion only stops the search and asks for it; the
// usual idle teardown (drop_unused_links(), driven by release_idle_engines())
// lets the link go over the next tick or two, and finish_deleting_boards()
// erases the board once nothing is left running in it.
static long long board_idle_ms = 24LL*60*60*1000;   // board_idle_ms= overrides, for the diagnostic
static long long empty_board_idle_ms = 60*1000;     // Session::untouched() ones (#111), empty_board_idle_ms=
static std::map<std::string, long long> board_alone_since;
static std::set<std::string> boards_to_delete;

static bool engine_set_busy(const Engine_Set& set)
{
    if(set.active>=0)
    return true;
    for(const std::unique_ptr<Engine_Link>& link : set.links)
    if(link && link->searching_now())
    return true;
    return false;
}

static void mark_board_for_deletion(const std::string& raw_id)
{
    if(!sessions.has(raw_id))
    return;
    Session& session = sessions.get(raw_id);
    session.analysis_on = false;
    session.watch_running = false;
    session.watch_step = false;
    abort_search(session);
    boards_to_delete.insert(session.id);
}

// Erases every marked board whose engines (if it has any) have gone quiet.
static void finish_deleting_boards()
{
    for(auto it = boards_to_delete.begin(); it!=boards_to_delete.end();)
    {
        auto eit = engines.find(*it);
        if(eit!=engines.end() && engine_set_busy(*eit->second))
        {
            ++it;
            continue;
        }
        if(eit!=engines.end())
        engines.erase(eit);
        board_alone_since.erase(*it);
        sessions.remove(*it);
        it = boards_to_delete.erase(it);
    }
}

// Once a tick: any board nothing has watched for board_idle_ms, or for
// empty_board_idle_ms while it is still untouched, is marked for deletion too.
static void expire_idle_boards()
{
    long long now = now_ms();
    for(const std::string& id : sessions.ids())
    {
        if(boards_to_delete.count(id))
        continue;
        if(server.subscribers(id)>0)
        {
            board_alone_since.erase(id);
            continue;
        }
        auto it = board_alone_since.find(id);
        if(it==board_alone_since.end())
        board_alone_since[id] = now;
        else if(now-it->second>=(sessions.get(id).untouched() ? std::min(empty_board_idle_ms, board_idle_ms) : board_idle_ms))
        mark_board_for_deletion(id);
    }
}

// Plays the move a Play or Watch search ended in (or a PTT entry stood in for,
// #79), credits the increment and notes what the search found with the move.
static void play_engine_move(Session& session, Search_Kind kind, const std::string& bestmove, const Search_Info& info)
{
    bool mover_was_white = session.white_to_move();
    std::string error;
    if(!session.play(bestmove, error))
    {
        session.engine_error = "the engine answered " + bestmove + " — " + error;
        session.watch_pause();
        return;
    }
    session.clock_credit_increment(mover_was_white);
    session.annotate_last(info, mover_was_white);
    if(kind==Search_Kind::WATCH)
    {
        // One step is one move: whatever it found, the game pauses here. A game
        // that just ended pauses too, so the button reads "Start" again.
        session.watch_step = false;
        std::string reason;
        if(session.result(reason)!=Outcome::ONGOING)
        session.watch_running = false;
    }
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
    int slot = kind==Search_Kind::WATCH ? watch_slot(session) : SLOT_SOLO;
    Go_Limits limits = kind==Search_Kind::ANALYSIS ? Go_Limits::analysis()
                     : session.clocked_now()       ? session.clock_go_limits(now_ms())
                     : kind==Search_Kind::PLAY     ? session.limits
                                                    : session.watch_limits_now();
    // A current PTT entry deep enough for this move is the move (#79): played
    // at once, with the clock as it stands. The wake brings the next tick,
    // which asks for the move after it.
    Search_Info stored;
    if(kind!=Search_Kind::ANALYSIS && session.ptt_move_now(limits, stored))
    {
        play_engine_move(session, kind, stored.pv[0], stored);
        server.wake();
        return true;
    }
    if(!engine_room(set, slot))
    {
        session.engine_error = engines_full();   // a move, Start or the analysis switch asks again
        session.watch_pause();
        return true;                              // the callers broadcast: the page shows why
    }
    Search_Request request;
    request.start_fen = session.root_fen();
    request.moves = session.moves();
    request.white_to_move = session.white_to_move();
    request.options = session.engine_options();
    // MultiPV (#69) only for an analysis: a Play or Watch search, which ends in
    // a move, is the plain one. Named every time, since Play and Analyse share
    // a process.
    request.options.push_back({"MultiPV", std::to_string(kind==Search_Kind::ANALYSIS ? session.analysis_lines : 1)});
    request.lines = kind==Search_Kind::ANALYSIS ? session.analysis_lines_now() : 1;
    // A full update's step (#100) searches to its save's depth.
    if(kind==Search_Kind::ANALYSIS && session.corr_update_limit()>0)
    {
        limits.infinite = false;
        limits.depth = session.corr_update_limit();
    }
    request.limits = limits;
    // An analysis is one at any limit (the path refresh and a marked root's
    // set-aside, #100); named every time, since Play and Analyse share a process.
    request.options.push_back({"UCI_AnalyseMode", kind==Search_Kind::ANALYSIS ? "true" : "false"});
    // Any other current entry seeds the search with its line (PTT_SEED).
    if(kind==Search_Kind::ANALYSIS ? session.analysis_lines_now()==1 : true)
    request.hint = session.ptt_hint();
    if(kind==Search_Kind::ANALYSIS)
    request.routes = session.transposition_routes();
    // A "ucinewgame" only when this engine has not seen this game before: in
    // Watch mode each process meets the game once, and in Analyse mode
    // moving around a game is not a reason to throw its table away.
    request.new_game = set.slot_game[slot]!=session.game_serial();
    // The Searcher (#101) keeps its TT across Returns (#100) and starts over
    // for a new root, or once it holds marks outside the mode. A new one holds
    // every save to the root; one going on gets what the generation brought.
    const bool corr = session.mode==Mode::CORRESPONDENCE;
    if(slot==SLOT_SOLO && (session.corr_searcher_fresh || (!corr && session.corr_searcher_marked)))
    {
        request.new_game = true;
        session.corr_searcher_fresh = false;
        session.corr_searcher_marked = false;
    }
    if(kind==Search_Kind::ANALYSIS && corr)
    {
        request.prelude_new = session.corr_all_marks();
        request.prelude = request.new_game ? request.prelude_new : session.corr_searcher_prelude;
        session.corr_searcher_prelude.clear();
        session.corr_searcher_marked = session.corr_searcher_marked || !session.corr_saves.empty();
    }
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
        session.corr_update_step_done();   // a full update's step (#100): saved, then the next
        broadcast(session);
        return;
    }
    session.ptt_offer(result.info);   // about the position it was played in: before the move
    play_engine_move(session, kind, result.bestmove, result.info);
    broadcast(session);
}

// ------------------------------------------------------ correspondence root (#101)
//
// The root engine runs "go infinite" on the root for as long as the session is
// in Correspondence mode and a page is watching. A Save or a Return asks for a
// restart (Session::corr_restart): the search is stopped, and once its bestmove
// is in, a new one starts with the prelude (ttmark/ttdemote) in front. A new
// root, or a new process, starts a new game holding every save instead.

static void abort_root(Engine_Set& set, Session& session)
{
    if(set.root_awaiting && set.links[SLOT_ROOT])
    set.links[SLOT_ROOT]->abort();
    set.root_awaiting = 0;
    session.corr_running = false;
}

static bool maybe_start_root(Session& session)
{
    Engine_Set& set = engines_of(session);
    if(session.mode!=Mode::CORRESPONDENCE || abandoned(set))
    {
        if(set.root_awaiting)
        abort_root(set, session);
        return false;
    }
    if(set.root_awaiting)
    {
        if(!session.corr_restart)
        return false;
        abort_root(set, session);   // its bestmove comes back and is dropped; then the new one starts
    }
    if(!session.corr_restart && (session.corr_finished || !session.corr_error.empty()))
    return false;
    if(!engine_room(set, SLOT_ROOT))
    {
        session.corr_error = engines_full();
        session.corr_restart = false;
        return true;
    }
    Engine_Link& link = link_of(set, SLOT_ROOT);
    if(link.searching_now())
    return false;
    Search_Request request;
    request.start_fen = session.root_fen();
    request.moves = session.corr_root_moves();
    request.limits = Go_Limits::analysis();
    request.options = session.engine_options();
    request.options.push_back({"MultiPV", "1"});
    request.new_game = set.slot_game[SLOT_ROOT]!=session.corr_serial;
    request.prelude = session.corr_root_prelude(request.new_game);
    long long token = link.start_search(request);
    if(!token)
    return false;
    set.slot_game[SLOT_ROOT] = session.corr_serial;
    set.root_awaiting = token;
    session.corr_prelude.clear();
    session.corr_restart = false;
    session.corr_running = true;
    session.corr_finished = false;
    session.corr_error.clear();
    return true;
}

// The root search's iterations and answers, and its end. True when the page
// has something new to show.
static bool collect_root(Session& session)
{
    Engine_Set& set = engines_of(session);
    if(!set.links[SLOT_ROOT])
    return false;
    Engine_Link& link = *set.links[SLOT_ROOT];
    bool changed = false;
    std::vector<std::string> notes;
    if(link.take_notes(notes))
    {
        for(const std::string& note : notes)
        session.corr_note(note);
        changed = true;
    }
    Search_Info progress;
    if(set.root_awaiting && link.take_progress(progress))
    {
        session.corr_info = progress;
        session.corr_info_valid = true;
        changed = true;
    }
    Search_Result result;
    if(!link.take_result(result) || result.token!=set.root_awaiting)
    return changed;
    set.root_awaiting = 0;
    session.corr_running = false;
    if(!result.error.empty())
    {
        session.corr_error = result.error;
        set.slot_game[SLOT_ROOT] = -1;   // a new process, which gets every save
    }
    else
    session.corr_finished = true;    // out of depth: starting it again would only spin
    return true;
}

// A closed tab must not leave engines thinking. Once no page has been on a
// session's stream for this long, its self-play game is paused, its analysis
// toggle goes off, and all its engine processes are let go; opening the page
// again starts them back up. The grace period (ENGINE_IDLE_MS) is what keeps a
// page reload — which drops the stream for a moment — from counting as leaving.

// Which engines the session's mode can still use. An engine this says nothing
// about is a process sitting on a transposition table for no one, so it goes:
// the Watch pair left behind by a switch back to Analyse is a few hundred MB
// of the room this repo deliberately keeps for running several engines at once.
static bool slot_wanted(const Session& session, int slot)
{
    if(session.mode==Mode::WATCH)      // the solo engine only to analyse a paused game
    return slot==SLOT_WHITE || (slot==SLOT_BLACK && !session.watch_pure)
        || (slot==SLOT_SOLO && session.analysis_on);
    if(session.mode==Mode::CORRESPONDENCE)
    return slot==SLOT_SOLO || slot==SLOT_ROOT;
    return slot==SLOT_SOLO;
}

static void release_idle_engines(const std::string& id, Session& session)
{
    Engine_Set& set = engines_of(session);
    if(server.subscribers(id)>0)
    set.alone_since = 0;
    else if(!set.alone_since)
    set.alone_since = now_ms();
    const bool gone = abandoned(set);

    // Nobody is watching: stop whatever is thinking first. Its bestmove still
    // has to arrive before the worker is idle, so the processes themselves go
    // on a later tick, through the loop below. The root search (#101) stops by
    // itself (maybe_start_root()) and starts again when a page comes back.
    if(gone && (session.watch_running || session.watch_step || session.analysis_on || set.active>=0))
    {
        session.analysis_on = false;
        abort_search(session);
        return;
    }
    drop_unused_links(session, set, gone);
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
            bool root_news = collect_root(session);
            if(maybe_start_root(session) || root_news)
            broadcast(session);
        }
        ticking = ticking || session.clock_ticking();
        release_idle_engines(id, session);
    }
    any_clock_ticking = ticking;
}

// The header's page counter (#68): how many pages are open on this server, in
// every session, and how many of them came through the tunnel or the LAN. Sent
// to every stream whenever that changes, and whenever a stream opens, so a new
// page gets it without asking.
static void publish_audience()
{
    static Http_Server::Audience sent{-1, -1};
    static long long sent_opened = -1;
    Http_Server::Audience now = server.audience();
    if(now==sent && server.streams_opened==sent_opened)
    return;
    sent = now;
    sent_opened = server.streams_opened;
    json::Out o;
    o.obj().key("pages").num(now.pages).key("remote").num(now.remote).end_obj();
    server.publish_all("audience", o.s);
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

static Response handle_get_authed(const Request& req)
{
    // What the page needs to know about the server itself: whether to offer
    // a logout.
    if(req.path=="/api/server")
    {
        json::Out o;
        o.obj().key("login").boolean(login.on()).end_obj();
        return Response::json(o.s);
    }

    Response refused;
    if(req.path.compare(0, 5, "/api/")==0 && board_refused(req.param("id"), refused))
    return refused;

    if(req.path=="/api/state")
    return Response::json(sessions.get(req.param("id")).state_json());

    if(req.path=="/api/events")
    return Response::event_stream(sessions.get(req.param("id")).id);

    // The event stream as a long poll, for a page behind a quick tunnel.
    if(req.path=="/api/wait")
    return Response::long_poll(sessions.get(req.param("id")).id, atoll(req.param("after").c_str()), req.param("page"));

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

static Response handle_post_authed(const Request& req)
{
    std::map<std::string, std::string> body;
    if(!req.body.empty() && !json::parse_flat_object(req.body, body))
    return Response::json(json::error("request body is not a flat JSON object"), 400);

    // Deleting a board (#112) is answered here, before the board cap below:
    // it never makes a board (a nonexistent id is just a 404) and must never
    // be refused by the cap that keeps new ones out. The board itself goes
    // once its engines, if any, are quiet (finish_deleting_boards()), so the
    // reply only confirms the request, not that the board is already gone.
    if(req.path=="/api/board-delete")
    {
        auto delete_id = body.find("id");
        std::string raw_id = delete_id!=body.end() ? delete_id->second : req.param("id");
        if(!sessions.has(raw_id))
        return Response::json(json::error("no such board"), 404);
        mark_board_for_deletion(raw_id);
        return Response::json("{}");
    }

    Response refused;
    auto id = body.find("id");
    if(board_refused(id!=body.end() ? id->second : req.param("id"), refused))
    return refused;
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
        if(session.mode==Mode::PLAY && session.paused)
        return Response::json(json::error("the game is paused"), 409);
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
        // covers, as a space-separated list of UCI moves. From the cursor, or
        // from `node` (the correspondence root's line starts at the root, #100).
        auto moves = body.find("moves");
        if(moves==body.end())
        return Response::json(json::error("no moves given"), 400);
        if(session.thinking())
        return Response::json(json::error("the engine is still thinking"), 409);
        auto node = body.find("node");
        ok = session.enter_line(moves->second, error, node==body.end() ? -1 : std::atoi(node->second.c_str()));
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
    else if(req.path=="/api/pause")
    {
        // Play mode's pause: the engine stops (a search under way is dropped, and
        // asked for again on Resume), and so does the clock, which is what makes
        // this different from thinking about your move. Watch has Start/Pause.
        auto on = body.find("on");
        if(on==body.end() || (on->second!="true" && on->second!="false"))
        return Response::json(json::error("on must be true or false"), 400);
        if(session.mode!=Mode::PLAY)
        return Response::json(json::error("only a game against the engine pauses here"), 409);
        bool want = on->second=="true";
        if(want!=session.paused)
        {
            abort_search(session);         // also what an analysis running under the pause is
            session.paused = want;
            if(!want)
            {
                session.analysis_on = false;   // the analysis was for the pause, not the game
                session.clock_sync(now_ms());  // your clock runs again from this press
            }
        }
    }
    else if(req.path=="/api/adjudicate")
    {
        auto given = body.find("result");
        if(given==body.end() || (given->second!="white" && given->second!="black" && given->second!="draw"))
        return Response::json(json::error("result must be white, black or draw"), 400);
        std::string reason;
        if(session.analyse_like())
        return Response::json(json::error("there is no game to adjudicate in Analyse mode"), 409);
        if(session.result(reason)!=Outcome::ONGOING)
        return Response::json(json::error("the game is already over"), 409);
        abort_search(session);
        session.adjudicate(given->second=="white" ? Outcome::WHITE_WINS
                         : given->second=="black" ? Outcome::BLACK_WINS : Outcome::DRAW);
    }
    else if(req.path=="/api/mode")
    {
        auto mode = body.find("mode");
        Mode parsed;
        if(mode==body.end() || !mode_from_name(mode->second, parsed))
        return Response::json(json::error("mode must be analyse, play, watch or correspondence"), 400);
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
        else if(parsed==Mode::CORRESPONDENCE)
        session.start_correspondence();   // a fresh board whose start is the root; a FEN moves it
        else
        {
            session.mode = parsed;
            // Watch keeps the board as it is, but its clocks are its own: from
            // Analyse they would otherwise still read 0:00 until a preset was
            // clicked, though the 1+1 it starts on is already set (#58).
            session.reseed_clock(0);
            session.reseed_clock(1);
        }
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
        // A game that is on takes a new setting only while paused, and goes on
        // from where it is. Before the first move there is nothing to keep.
        bool going = session.mode==Mode::PLAY && session.has_moves();
        if(going && !session.paused)
        return Response::json(json::error("pause the game to change its settings"), 409);
        abort_search(session);
        session.side_choice = side;
        session.limits = limits;
        session.play_clock_on = clock_on;
        session.play_base_ms[0] = base_ms[0];
        session.play_base_ms[1] = base_ms[1];
        session.play_inc_ms[0] = inc_ms[0];
        session.play_inc_ms[1] = inc_ms[1];
        if(!going)
        session.start_play();
        else
        {
            if(given!=body.end())
            session.choose_side();
            if(setting.given && clock_on)
            {
                session.reseed_clock(0);
                session.reseed_clock(1);
            }
        }
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
                // Like Play: a game that is on takes it only while paused, and goes on.
                if(session.mode==Mode::WATCH && session.has_moves())
                {
                    if(!session.paused_now())
                    return Response::json(json::error("pause the game to change its settings"), 409);
                    abort_search(session);
                }
                else
                restart = true;
                session.watch_clock_on = setting.clock;
                if(setting.clock)
                {
                    session.watch_base_ms[i] = setting.base_ms;
                    session.watch_inc_ms[i] = setting.inc_ms;
                }
                else
                session.watch_limits[i].depth = setting.depth;
                if(!restart && setting.clock)
                session.reseed_clock(i);
            }
        }
        if(restart)
        {
            abort_search(session);
            session.start_watch();
        }
        // Pure watch (#91): one process for both sides, or one each. Any time,
        // even mid-game: it decides only which process the next move asks.
        auto pure = body.find("pure");
        if(pure!=body.end())
        {
            if(pure->second!="true" && pure->second!="false")
            return Response::json(json::error("pure must be true or false"), 400);
            session.watch_pure = pure->second=="true";
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
                if(session.analysing())
                abort_search(session);           // the analysis was for the pause
                session.analysis_on = false;
                session.watch_running = action->second=="start";
                session.watch_step = action->second=="step";
                session.clock_sync(now_ms());   // arm the resuming mover's clock from this press
            }
            else
            return Response::json(json::error("action must be start, pause or step"), 400);
        }
    }
    else if(req.path=="/api/corr")
    {
        // Correspondence mode (#101): enter (the position on the board becomes
        // the root), save (Save to root, marked with the generation, #100),
        // return (Return to root), update (the full update), stop-update or
        // probe (Ask the root, #100).
        auto action = body.find("action");
        if(action!=body.end())
        {
            if(action->second=="enter")
            {
                if(!session.analyse_like())
                return Response::json(json::error("correspondence mode starts from Analyse"), 409);
                session.corr_enter();
            }
            else if(session.mode!=Mode::CORRESPONDENCE)
            return Response::json(json::error("not in correspondence mode"), 409);
            else if(action->second=="save")
            ok = session.corr_save(error);
            else if(action->second=="return")
            {
                abort_search(session);   // the Searcher starts again with its prelude
                session.corr_return();
            }
            else if(action->second=="update")
            {
                abort_search(session);
                ok = session.corr_update_start(error);
            }
            else if(action->second=="stop-update")
            session.corr_update_stop("stopped");
            else if(action->second=="probe")
            session.corr_probe_ask();   // the root engine's entry for the board's position (#100)
            else
            return Response::json(json::error("action must be enter, save, return, update, stop-update or probe"), 400);
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
            { "advanced",   &Session::show_advanced    },
            { "dreamer",    &Session::show_dreamer     },
        };
        const bool was_checked = session.show_advanced || session.show_dreamer;
        const bool had_dreamer = session.show_dreamer;
        for(const auto& option : switches)
        {
            auto given = body.find(option.name);
            if(given==body.end())
            continue;
            if(given->second!="true" && given->second!="false")
            return Response::json(json::error(std::string(option.name) + " must be true or false"), 400);
            session.*option.field = given->second=="true";
        }
        // Advanced debugging or the dreamer just went on: check the breakdown
        // (and the dreamer's fast db) against the real eval (#66), ~1-2 ms.
        if((session.show_advanced || session.show_dreamer) && (!was_checked || session.show_dreamer!=had_dreamer))
        session.run_eval_check();
        // The tablebase switch and piece limit (#40). The tables are the server's,
        // so the switch cannot be turned on without them. An engine already
        // searching keeps the options it started with; the next search sends them.
        auto tb = body.find("tb");
        if(tb!=body.end())
        {
            if(tb->second!="true" && tb->second!="false")
            return Response::json(json::error("tb must be true or false"), 400);
            if(tb->second=="true" && !tablebase_setup().available())
            return Response::json(json::error(tablebase_setup().reason), 409);
            session.tb_on = tb->second=="true";
        }
        auto limit = body.find("tbLimit");
        if(limit!=body.end())
        {
            int n = std::atoi(limit->second.c_str());
            if(n<TB_LIMIT_MIN || n>TB_LIMIT_MAX)
            return Response::json(json::error("tbLimit must be 3, 4 or 5"), 400);
            session.tb_limit = n;
        }
        // The analysis's number of lines (MultiPV, #69), from the Engine panel.
        // A running analysis starts again with it; what was kept stays, and
        // shows as many of its lines as are wanted now.
        auto lines = body.find("lines");
        if(lines!=body.end())
        {
            int n = std::atoi(lines->second.c_str());
            if(n<1 || n>ANALYSIS_LINES_MAX)
            return Response::json(json::error("lines must be 1 to " + std::to_string(ANALYSIS_LINES_MAX)), 400);
            if(n!=session.analysis_lines)
            {
                session.analysis_lines = n;
                if(session.analysing())
                abort_search(session);
            }
        }
        // The eval-correction net (#53). A running analysis starts again under
        // the new eval, and what was kept from the old one is forgotten; a Play or
        // Watch search keeps its options, like the tables, and the next move has them.
        auto nne = body.find("nne");
        if(nne!=body.end())
        {
            if(nne->second!="true" && nne->second!="false")
            return Response::json(json::error("nne must be true or false"), 400);
            if(session.nne_on!=(nne->second=="true"))
            {
                session.nne_on = nne->second=="true";
                if(session.analysing())
                abort_search(session);
                session.forget_analysis();
                // the breakdown and the dreamer count the net too (#67)
                if(session.show_advanced || session.show_dreamer)
                session.run_eval_check();
            }
        }
        // Whether the session reads the PTT (#79), from the Engine panel. Storing
        // goes on either way; a kept result on show goes or comes back at once.
        auto ptt_use = body.find("pttUse");
        if(ptt_use!=body.end())
        {
            if(ptt_use->second!="true" && ptt_use->second!="false")
            return Response::json(json::error("pttUse must be true or false"), 400);
            if(session.ptt_use!=(ptt_use->second=="true"))
            {
                session.ptt_use = ptt_use->second=="true";
                session.recall_kept();
            }
        }
        // Whether an analysis refreshes the tree's other routes too (#100), from
        // the Engine panel. Counts from the next search on.
        auto routes = body.find("routes");
        if(routes!=body.end())
        {
            if(routes->second!="true" && routes->second!="false")
            return Response::json(json::error("routes must be true or false"), 400);
            session.routes_on = routes->second=="true";
        }
        // Narrowing on stored bounds (#65), only in an engine built with it.
        // Deeper is a step on top of narrow: it cannot be on alone, and turning
        // narrow off takes it along. Like the net, a change restarts the analysis.
        auto narrow = body.find("narrow");
        auto deeper = body.find("narrowDeeper");
        if(narrow!=body.end() || deeper!=body.end())
        {
            bool want_narrow = session.narrow_on, want_deeper = session.narrow_deeper;
            for(auto given : { narrow, deeper })
            {
                if(given==body.end())
                continue;
                if(given->second!="true" && given->second!="false")
                return Response::json(json::error(given->first + " must be true or false"), 400);
                (given==narrow ? want_narrow : want_deeper) = given->second=="true";
            }
            if((want_narrow || want_deeper) && !engine_build().narrowing)
            return Response::json(json::error("this engine is built without narrowing (TT_BOUNDS_NEVER_NARROW)"), 409);
            if(want_deeper && !want_narrow)
            {
                if(deeper!=body.end())
                return Response::json(json::error("narrowDeeper needs narrow on"), 409);
                want_deeper = false;   // narrow switched off: deeper goes with it
            }
            if(want_narrow!=session.narrow_on || want_deeper!=session.narrow_deeper)
            {
                session.narrow_on = want_narrow;
                session.narrow_deeper = want_deeper;
                if(session.analysing())
                abort_search(session);
                session.forget_analysis();
            }
        }
    }
    else
    return Response::text("not found: " + req.path, 404);

    if(!ok)
    return Response::json(json::error(error), 400);
    // A move the human just made, or a fresh game the engine has white in:
    // ask for its reply before answering, so the page sees "thinking" at once.
    maybe_start_search(session);
    maybe_start_root(session);                        // a Save or a Return restarts the root (#101)
    broadcast(session);                               // every other open page follows along
    return Response::json(session.state_json());
}

// The token check runs before either _authed function, so a request that
// fails it never reaches session_of() — no session is created and no engine
// is touched for someone who only guessed at a path. A request authenticated
// by its query token gets the cookie back, so the browser's next request (the
// very next asset load, in practice) carries it instead.
static Response handle_get(const Request& req)
{
    // Which release is online (#110), the one route without a login: a curl
    // on the server checks an update, and it tells no more than the public
    // repository does. The engine's own pair shows a mismatched binary.
    if(req.path=="/api/version")
    {
        json::Out o;
        o.obj().key("version").str(APP_VERSION).key("commit").str(ENGINE_COMMIT)
         .key("engineVersion").str(engine_build().version).key("engineCommit").str(engine_build().commit).end_obj();
        return Response::json(o.s);
    }
    Auth auth = check_auth(req);
    if(!auth.ok && login.on())
    {
        // Not logged in: the API says so, the icon is free (a login page has
        // one too), and a page goes to the login and comes back afterwards.
        if(req.path.compare(0, 5, "/api/")==0)
        return Response::json(json::error("not logged in"), 401);
        std::string icon;
        if(req.path=="/favicon.png" && read_web_file(req.path, icon))
        return Response::file(icon, mime_type(req.path));
        std::string next = gui_access::safe_next(req.path + (req.query.empty() ? "" : "?" + req.query));
        return redirect(next=="/" ? "/login" : "/login?next=" + gui_access::url_encode(next));
    }
    if(!auth.ok)
    return Response::text("unauthorized", 401);
    Response res = handle_get_authed(req);
    if(auth.set_cookie)
    res.set_cookie = auth_cookie();
    return res;
}

static Response handle_post(const Request& req)
{
    Auth auth = check_auth(req);
    if(!auth.ok)
    return Response::json(json::error(login.on() ? "not logged in" : "unauthorized"), 401);
    Response res = handle_post_authed(req);
    if(auth.set_cookie)
    res.set_cookie = auth_cookie();
    return res;
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

// Every request: the login routes first, then by method.
static Response route(const Request& req)
{
    if(req.method=="POST" && gui_access::foreign_origin(req.headers))
    return Response::text("refused: this request comes from another site's page", 403);
    Response login_response;
    if(login.on() && handle_login_routes(req, login_response))
    return login_response;
    if(req.method=="GET")
    return handle_get(req);
    if(req.method=="POST")
    return handle_post(req);
    if(req.method=="HEAD" || req.method=="OPTIONS")
    return Response::text("");
    return Response::text("method not allowed: " + req.method, 405);
}

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
    std::string root_option, start_fen, engine_option, bind_option = "127.0.0.1", tunnel_option, syzygy_option, gaviota_option;
    const char* home = std::getenv("HOME");
    std::string ptt_option = home ? std::string(home) + "/.ascaniusfish/ptt.txt" : "";
    // The limits: the environment first (a container sets them there), an
    // option on top.
    std::map<std::string, std::string> limits = {
        {"max_connections", std::to_string(DEFAULT_MAX_CONNECTIONS)}, {"max_engines", "2"}, {"max_boards", "20"},
        {"login_fails_per_ip", std::to_string(gui_access::DEFAULT_FAILS_PER_IP)},
        {"login_fails_total", std::to_string(gui_access::DEFAULT_FAILS_TOTAL)}};
    for(auto& limit : limits)
    {
        std::string var = "GUI_" + limit.first;
        std::transform(var.begin(), var.end(), var.begin(), ::toupper);
        if(const char* v = std::getenv(var.c_str()))
        limit.second = v;
    }
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
        else if(key=="bind") bind_option = value;
        else if(key=="root") root_option = value;
        else if(key=="fen")  start_fen = value;
        else if(key=="engine") engine_option = value;
        else if(key=="syzygy") syzygy_option = value;
        else if(key=="gaviota") gaviota_option = value;
        else if(key=="ptt")     ptt_option = value=="none" ? "" : value;
        else if(key=="tunnel") tunnel_option = value;
        else if(key=="board_idle_ms") board_idle_ms = std::atoll(value.c_str());   // #112, for the diagnostic
        else if(key=="empty_board_idle_ms") empty_board_idle_ms = std::atoll(value.c_str());   // #111, likewise
        else if(limits.count(key)) limits[key] = value;
        else
        {
            std::fprintf(stderr, "gui: unknown option \"%s\"\n", key.c_str());
            return 2;
        }
    }
    if(!tunnel_option.empty() && tunnel_option!="cloudflared")
    {
        std::fprintf(stderr, "gui: tunnel= only supports \"cloudflared\" (got \"%s\")\n", tunnel_option.c_str());
        return 2;
    }

    for(auto& limit : limits)
    {
        char* end = nullptr;
        long n = std::strtol(limit.second.c_str(), &end, 10);
        if(limit.second.empty() || *end || n<1 || n>100000)
        {
            std::fprintf(stderr, "gui: %s must be a whole number from 1 (got \"%s\")\n", limit.first.c_str(), limit.second.c_str());
            return 2;
        }
    }
    server.max_connections = (size_t)std::atol(limits["max_connections"].c_str());
    max_engines = std::atoi(limits["max_engines"].c_str());
    max_boards = (size_t)std::atol(limits["max_boards"].c_str());
    login_limiter.per_ip = std::atoi(limits["login_fails_per_ip"].c_str());
    login_limiter.total = std::atoi(limits["login_fails_total"].c_str());

    // The password and the secret leave the environment once read: the
    // engine processes started later inherit it, and have no use for them.
    const char* trust = std::getenv("GUI_TRUST_PROXY");
    trust_proxy = trust && std::string(trust)=="1";
    if(const char* password = std::getenv("GUI_PASSWORD"))
    {
        const char* secret = std::getenv("GUI_COOKIE_SECRET");
        std::string error;
        bool ok = login.setup(password, secret ? secret : "", error);
        unsetenv("GUI_PASSWORD");
        unsetenv("GUI_COOKIE_SECRET");
        if(!ok)
        {
            std::fprintf(stderr, "gui: %s\n", error.c_str());
            return 2;
        }
    }
    unsetenv("GUI_COOKIE_SECRET");

    gui_token = generate_token();

    if(!syzygy_option.empty())
    {
        tablebase_setup().load(syzygy_option);
        std::printf("Tablebases: %d WDL tables from %s\n", tablebase_setup().tables, syzygy_option.c_str());
    }
    if(!gaviota_option.empty())
    {
        tablebase_setup().load_dtm(gaviota_option);
        if(tablebase_setup().dtm_pieces)
        std::printf("DTM tables: up to %d pieces from %s\n", tablebase_setup().dtm_pieces, gaviota_option.c_str());
        else
        std::printf("DTM tables: none in %s%s\n", gaviota_option.c_str(), gaviota::compiled_in() ? "" : " (built without -DWITH_GAVIOTA)");
    }

    engine_path = find_engine(engine_option);
    engine_build().probe(engine_path);
    std::printf("Engine: %s, %s\n", engine_path.c_str(), !engine_build().known ? "did not answer uci"
                : engine_build().narrowing ? "narrowing build (TTNarrowing/TTNarrowingDeeper)" : "stored bounds only cut");
    {
        std::string error;
        if(!persistent_tt().open(ptt_option, error))
        std::fprintf(stderr, "gui: ptt: %s (searches are not kept)\n", error.c_str());
        else if(persistent_tt().on())
        std::printf("PTT: %zu positions in %s (eval version %d%s)\n", persistent_tt().size(), ptt_option.c_str(),
                    engine_build().eval_version, engine_build().net_hash.empty() ? ", no net" : "");
        else
        std::printf("PTT: off\n");
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
        Response res = route(req);
        // HSTS only on what came over https: sent over plain http a browser
        // ignores it anyway (#111).
        res.hsts = trust_proxy && gui_access::client_of(req.peer, req.headers, true).https;
        return res;
    };
    // A page that just connected has no position yet; send it one.
    server.on_subscribe = [](const std::string& topic) { broadcast(sessions.get(topic)); };
    // Where a finished search becomes a move on the board: on this thread, so
    // nothing the worker produced ever touches a socket itself.
    server.on_tick = [] { collect_all(); finish_deleting_boards(); expire_idle_boards(); publish_audience(); };
    // A running clock is checked for a flag fall often; an idle server sits
    // at the old fixed interval and costs nothing.
    server.poll_timeout = [] { return any_clock_ticking ? CLOCK_POLL_MS : IDLE_POLL_MS; };

    if(!server.listen_on(port, bind_option))
    {
        std::fprintf(stderr, "gui: could not bind %s on a port from %d upwards"
                              " (bind= must be a valid IPv4 address)\n", bind_option.c_str(), port);
        return 2;
    }

    std::printf("Limits: %zu connections, %d engines, %zu boards; %d failed logins per address, %d in all, per 5 min\n",
                server.max_connections, max_engines, max_boards, login_limiter.per_ip, login_limiter.total);
    // With a password: no token, and nothing about the password, since this
    // output may well end up in a container's log.
    if(login.on())
    std::printf("Ascaniusfish GUI on port %d (serving %s, Ctrl-C to stop)\n"
                "Password login on%s.\n",
                server.port(), web_root.c_str(), trust_proxy ? ", client address and https from the proxy's headers" : "");
    else
    std::printf(
        "Ascaniusfish GUI on http://localhost:%d  (serving %s, Ctrl-C to stop)\n"
        "Token for this run: %s\n"
        "Local link: http://localhost:%d/?token=%s\n",
        server.port(), web_root.c_str(), gui_token.c_str(), server.port(), gui_token.c_str());
    if(tunnel_option=="cloudflared")
    {
        std::printf("Starting a Cloudflare quick tunnel (cloudflared)...\n");
        std::fflush(stdout);
        std::string error;
        std::string url = start_tunnel_and_wait(server.port(), 20000, error);
        if(!url.empty())
        {
            std::string share_url = url + (login.on() ? "/" : "/?token=" + gui_token);
            std::printf("Shareable link: %s\n"
                        "(quick tunnels can take a while to actually route through Cloudflare —\n"
                        " checking now, will print a confirmation once it's actually live)\n",
                        share_url.c_str());
            std::thread(confirm_tunnel_reachable, share_url).detach();
        }
        else
        std::fprintf(stderr, "gui: tunnel: %s\n", error.c_str());
    }
    else if(!login.on())
    std::printf("Append the same \"?token=...\" to a LAN IP or a tunnel's URL to share it\n"
                 "(see docs/REMOTE_PLAY.md, or pass tunnel=cloudflared to do it automatically).\n");
    if(!login.on())
    std::printf("The page keeps the token in a cookie after the first open. A new run makes a new token.\n");
    std::fflush(stdout);
    server.run();
    std::printf("\nStopping.\n");
    quit_engines();
    stop_tunnel();
    std::fflush(nullptr);
    std::_Exit(0);   // anything still thinking is left to EOF on its stdin
}
