// OWNERSHIP=Claude
// One board per visitor, shared by link (#112): creating a board by its id,
// deleting one (POST /api/board-delete, a separate endpoint from the
// existing POST /api/delete, which removes a move), and the 24 h idle sweep
// (gui_server.cpp's expire_idle_boards()). Drives the real server, the same
// way diagnostics/gui_access_test.cpp's part 2 does.
//
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -o diagnostics/board_lifecycle_test diagnostics/board_lifecycle_test.cpp
//
// Run from the repo root after `make gui/ascaniusfish_gui ascaniusfish_uci`.
// Takes a few seconds (board_idle_ms= shrinks the 24 h sweep for the test;
// the engine-attached delete and the cap it frees are polled for, not slept
// for a fixed time). Exit code 1 on any failure.
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

static int failures = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if(!ok)
    failures++;
}

static int connect_to(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(connect(fd, (sockaddr*)&addr, sizeof addr)!=0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static std::string read_some(int fd, int timeout_ms)
{
    std::string got;
    auto end = std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
    for(;;)
    {
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()).count();
        if(left<=0)
        break;
        pollfd p = {fd, POLLIN, 0};
        if(poll(&p, 1, left)<=0)
        break;
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof buf);
        if(n<=0)
        break;
        got.append(buf, n);
    }
    return got;
}

struct Reply
{
    int status = 0;
    std::string head, body;
    std::string header(const std::string& name) const
    {
        size_t at = head.find("\r\n" + name + ": ");
        if(at==std::string::npos)
        return "";
        at += name.size()+4;
        return head.substr(at, head.find("\r\n", at)-at);
    }
};

static Reply request(int port, const std::string& method, const std::string& target, const std::string& headers = "",
                     const std::string& body = "")
{
    Reply r;
    int fd = connect_to(port);
    if(fd<0)
    return r;
    std::string req = method + " " + target + " HTTP/1.1\r\nHost: localhost\r\n" + headers;
    if(method=="POST")
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    if(write(fd, req.data(), req.size())<0) {}
    std::string got = read_some(fd, 5000);
    close(fd);
    size_t blank = got.find("\r\n\r\n");
    r.head = got.substr(0, blank);
    r.body = blank==std::string::npos ? "" : got.substr(blank+4);
    if(got.size()>12)
    r.status = std::atoi(got.c_str()+9);
    return r;
}

static const char* PASSWORD = "test password 112";
static const char* SECRET = "test-secret-for-the-diagnostic-0112";
static const char* LOG = "/tmp/board_lifecycle_test.log";

static pid_t server_pid = -1;

// Starts the server with a small board cap and shrunk idle-sweep windows
// (board_idle_ms=, #112; empty_board_idle_ms=, #111), so the sweep can be
// watched in a diagnostic rather than taken on faith. Returns its port, or 0.
static int start_server()
{
    unlink(LOG);
    server_pid = fork();
    if(server_pid==0)
    {
        int fd = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        dup2(fd, 1);
        dup2(fd, 2);
        setenv("GUI_PASSWORD", PASSWORD, 1);
        setenv("GUI_COOKIE_SECRET", SECRET, 1);
        setenv("GUI_MAX_BOARDS", "3", 1);
        setenv("GUI_MAX_ENGINES", "1", 1);
        execl("./gui/ascaniusfish_gui", "ascaniusfish_gui", "port=8392", "ptt=none", "engine=./ascaniusfish_uci",
              "board_idle_ms=1500", "empty_board_idle_ms=400", (char*)nullptr);
        _exit(127);
    }
    for(int i=0;i<200;i++)
    {
        std::ifstream log(LOG);
        std::stringstream text;
        text << log.rdbuf();
        size_t at = text.str().find("GUI on port ");
        if(at!=std::string::npos)
        return std::atoi(text.str().c_str()+at+12);
        usleep(50000);
    }
    return 0;
}

static void stop_server()
{
    if(server_pid>0)
    {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, nullptr, 0);
        server_pid = -1;
    }
}

static std::string login_body()
{
    std::string encoded;
    for(char c : std::string(PASSWORD))
    if(c==' ') encoded += "+"; else encoded += c;
    return "password=" + encoded + "&next=%2F";
}

// Logs in once and returns the "Cookie: ..." header every later request sends.
static std::string login(int port)
{
    Reply r = request(port, "POST", "/login", "", login_body());
    std::string set_cookie = r.header("Set-Cookie");
    return "Cookie: " + set_cookie.substr(0, set_cookie.find(';')) + "\r\n";
}

// Repeats `probe` (true = done) until it says so or timeout_ms passes. With
// nothing ticking, the server's own poll() waits up to IDLE_POLL_MS (30 s)
// between on_tick() calls — where finish_deleting_boards()/
// expire_idle_boards() run — so a connection is opened between tries to wake
// it promptly; /api/version touches no board and needs no login.
template<class Probe>
static bool poll_until(int port, const std::string& cookie, Probe probe, int timeout_ms)
{
    auto end = std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
    for(;;)
    {
        if(probe())
        return true;
        if(std::chrono::steady_clock::now()>=end)
        return false;
        request(port, "GET", "/api/version", cookie);
        usleep(30000);
    }
}

// "legalMoves" lists e4 as a legal reply on a virgin board too, so a played
// move has to be told apart by the position itself, not by a SAN appearing
// anywhere in the state.
static bool has_moves(const Reply& r)
{
    return r.body.find("\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\"")==std::string::npos;
}

int main()
{
    int port = start_server();
    check(port>0, "the server starts");
    if(!port)
    {
        std::printf("FAILED: could not start the server, see %s\n", LOG);
        return 1;
    }
    std::string cookie = login(port);
    check(!cookie.empty() && cookie!="Cookie: \r\n", "logging in gives a cookie");

    // ---------------------------------------------------------- create and the cap
    // GUI_MAX_BOARDS=3 throughout, so every scenario below deletes (or lets
    // expire) what it made before the next one creates anything, and never
    // more than 3 boards — "main" among them — are alive at once.
    check(request(port, "GET", "/api/state?id=main", cookie).status==200, "the startup board exists");
    check(request(port, "GET", "/api/state?id=b1", cookie).status==200, "a fresh id makes a board (2 of 3)");
    check(request(port, "GET", "/api/state?id=b2", cookie).status==200, "and another (3 of 3)");
    Reply over_cap = request(port, "GET", "/api/state?id=b3", cookie);
    check(over_cap.status==503 && over_cap.body.find("maximum of 3 boards")!=std::string::npos,
          "a fourth is refused with a message");
    check(request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"b1\"}").status==200, "b1 deleted, to free room");
    check(request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"b2\"}").status==200, "b2 deleted, to free room");

    // ------------------------------------------------------------------- delete
    check(request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"nosuch\"}").status==404,
          "deleting a board that was never made is a 404");

    request(port, "POST", "/api/move", cookie, "{\"id\":\"d1\",\"uci\":\"e2e4\"}");
    check(has_moves(request(port, "GET", "/api/state?id=d1", cookie)), "d1 has the move played into it");
    Reply del = request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"d1\"}");
    check(del.status==200, "deleting d1 (no engine ever touched it) answers 200");
    check(!has_moves(request(port, "GET", "/api/state?id=d1", cookie)),
          "asking for d1 again starts a fresh board, the move is gone");
    check(request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"d1\"}").status==200, "d1 deleted again, to free room");

    // -------------------------------------------------- delete with an engine running
    // max_engines=1, so starting e2's analysis only succeeds once e1's engine
    // — mid-search when e1 is deleted — has actually been let go, not merely
    // asked to stop (drop_unused_links() cannot join a searching Engine_Link
    // without blocking the server, see gui_server.cpp's quit_engines()).
    request(port, "GET", "/api/state?id=e1", cookie);
    Reply start_analysis = request(port, "POST", "/api/analyse", cookie, "{\"id\":\"e1\",\"on\":\"true\"}");
    check(start_analysis.status==200, "e1 starts an analysis");
    check(request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"e1\"}").status==200,
          "deleting e1 while it is thinking is still answered at once");
    bool freed = poll_until(port, cookie, [&] {
        return request(port, "POST", "/api/analyse", cookie, "{\"id\":\"e2\",\"on\":\"true\"}").status==200;
    }, 5000);
    check(freed, "e1's engine is eventually let go, so e2 can start its own within 5 s");
    request(port, "POST", "/api/analyse", cookie, "{\"id\":\"e2\",\"on\":\"false\"}");
    check(request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"e2\"}").status==200, "e2 deleted, to free room");
    // The delete answers before e2's own search has actually stopped (just
    // like e1's above): wait for it to be fully gone — a second delete 404s
    // only once it really is — before leaning on the cap it frees.
    check(poll_until(port, cookie, [&] {
        return request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"e2\"}").status==404;
    }, 5000), "e2 is fully gone within 5 s");

    // ------------------------------------------------------------------- expiry
    // y1 is never watched by a stream: with board_idle_ms=1500 it should age
    // out within a few ticks. z1 keeps an SSE stream open throughout and must
    // survive the same wait.
    // "main" is deleted here rather than left to expire on its own clock:
    // every other board above was cleaned up as it went, but board_idle_ms
    // is short enough for the few seconds of polling above (e1's and e2's)
    // to have already aged it out — and whether that landed before or after
    // this point is exactly the kind of timing this step removes, so y1 and
    // z1 below always get the full cap of room to themselves.
    request(port, "POST", "/api/board-delete", cookie, "{\"id\":\"main\"}");

    // An untouched board goes after empty_board_idle_ms=400 (#111), one with a
    // move only after board_idle_ms=1500. w1, v1 and u1 fill the cap; w2 fits
    // once w1 is swept, while v1 still has its move.
    request(port, "GET", "/api/state?id=w1", cookie);
    request(port, "POST", "/api/move", cookie, "{\"id\":\"v1\",\"uci\":\"e2e4\"}");
    request(port, "POST", "/api/move", cookie, "{\"id\":\"u1\",\"uci\":\"e2e4\"}");
    check(request(port, "GET", "/api/state?id=w2", cookie).status==503, "w1, v1 and u1 fill the cap");
    bool emptied = poll_until(port, cookie, [&] {
        return request(port, "GET", "/api/state?id=w2", cookie).status==200;
    }, 1200);
    check(emptied, "the untouched board w1 is swept within 1.2 s, making room");
    check(has_moves(request(port, "GET", "/api/state?id=v1", cookie)), "v1, with a move, is not swept yet");
    for(const char* id : {"w2", "v1", "u1"})
    request(port, "POST", "/api/board-delete", cookie, std::string("{\"id\":\"") + id + "\"}");
    check(request(port, "POST", "/api/move", cookie, "{\"id\":\"y1\",\"uci\":\"e2e4\"}").status==200, "y1 made and played into");
    check(request(port, "POST", "/api/move", cookie, "{\"id\":\"z1\",\"uci\":\"e2e4\"}").status==200, "z1 made and played into");
    int watch_fd = connect_to(port);
    std::string sub = "GET /api/events?id=z1 HTTP/1.1\r\nHost: localhost\r\n" + cookie + "\r\n";
    if(write(watch_fd, sub.data(), sub.size())<0) {}
    read_some(watch_fd, 1000);   // the first "state" event, so the stream is actually open

    bool expired = poll_until(port, cookie, [&] {
        return !has_moves(request(port, "GET", "/api/state?id=y1", cookie));
    }, 5000);
    check(expired, "the unwatched board y1 is swept within 5 s at board_idle_ms=1500");
    check(has_moves(request(port, "GET", "/api/state?id=z1", cookie)), "the watched board z1 is not");
    close(watch_fd);

    stop_server();
    std::printf("%s: %d failure%s\n", failures ? "FAILED" : "passed", failures, failures==1 ? "" : "s");
    return failures ? 1 : 0;
}
