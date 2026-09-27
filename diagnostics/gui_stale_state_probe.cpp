// OWNERSHIP=Claude
// Why does dragging the engine's own suggested move leave the Analyse panel on
// "Starting the search..." for seconds, when any other move fills it at once? (#23)
//
// Nothing is slow. The GUI hands the page a state from two places at once:
//
//   POST /api/move  -> the answer, built after clear_analysis() and *before*
//                      the new search has started, so its "search" is null;
//   GET /api/events -> the SSE stream, which carries the new search's first
//                      finished iteration.
//
// Play the move the engine just suggested and the new root is already in its
// transposition table, so iterations 1..N come back in about a millisecond -
// inside the turn the browser spends on `res.json()` for the answer it is still
// holding. app.js used to apply whichever state arrived last, so the answer,
// older than the line it had already been given, overwrote it. The panel then
// sat empty until the next iteration finished, which after a warm table is the
// first iteration deeper than anything searched so far: seconds to a minute.
// An unexpected move has a cold root, its first line is hundreds of ms away,
// and the answer is long applied by then - hence "every other move is fine".
//
// The fix is a serial on every state (`seq`, gui/session.hpp) and a page that
// applies states in that order rather than in arrival order. This probe drives
// the real server over HTTP and, for each ply, judges the same states twice:
// as the page used to (arrival order) and as it does now (seq order). It prints
// how long the panel stays empty in each case, so the number the bug is worth
// is measured rather than asserted, and it fails if seq order ever loses a line.
//
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -o diagnostics/gui_stale_state_probe
//       diagnostics/gui_stale_state_probe.cpp
//
// Run it from the repo root, after `make gui/ascaniusfish_gui`, so the server
// and ./ascaniusfish_uci are where it looks for them. It speaks only HTTP, so
// it pulls in no engine header and builds in a second.
//
//   ./diagnostics/gui_stale_state_probe [soak_ms] [plies] [fen]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <vector>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

// A middlegame with plenty to think about, so a soak reaches a depth whose next
// iteration is slow - which is what makes the gap long enough to be annoying.
static const char* const PROBE_FEN =
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 1";
constexpr long long DEFAULT_SOAK_MS = 25000;
constexpr int DEFAULT_PLIES = 3;
// How long the browser can be between having the answer's bytes and having
// applied it: one `res.json()` turn. Every SSE state inside this window is one
// the page has already drawn when the answer lands on top of it.
constexpr long long JSON_TURN_MS = 20;
// How long to wait for the next iteration once the panel has been emptied.
constexpr long long NEXT_LINE_WAIT_MS = 180000;

static long long now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------ tiny JSON
// The probe wants four fields out of a state and nothing else, so it reads them
// by name rather than parsing. Enough for the writer in gui/json.hpp, which
// emits no spaces and escapes nothing that appears in these values.

// The value that follows "key": , as raw text up to the first delimiter.
static std::string field(const std::string& s, const std::string& key, size_t from = 0)
{
    size_t k = s.find("\"" + key + "\":", from);
    if(k==std::string::npos)
    return "";
    size_t v = k + key.size() + 3;
    if(v<s.size() && s[v]=='"')
    {
        size_t end = s.find('"', v+1);
        return end==std::string::npos ? "" : s.substr(v+1, end-v-1);
    }
    size_t end = s.find_first_of(",}]", v);
    return s.substr(v, (end==std::string::npos ? s.size() : end)-v);
}

static long long state_seq(const std::string& state)
{
    std::string v = field(state, "seq");
    return v.empty() ? -1 : std::atoll(v.c_str());
}

// The depth of the analysis line in this state, or -1 for "no line at all" -
// which is exactly what the page turns into "Starting the search...".
static int analysis_depth(const std::string& state)
{
    size_t a = state.find("\"analysis\":");
    if(a==std::string::npos)
    return -1;
    size_t s = state.find("\"search\":", a);
    if(s==std::string::npos || state.compare(s+9, 1, "{")!=0)
    return -1;
    std::string v = field(state, "depth", s);
    return v.empty() ? -1 : std::atoi(v.c_str());
}

// The first move of the analysis line: the one a user drags onto the board.
static std::string analysis_first_move(const std::string& state)
{
    size_t a = state.find("\"analysis\":");
    if(a==std::string::npos)
    return "";
    size_t line = state.find("\"line\":[", a);
    return line==std::string::npos ? "" : field(state, "uci", line);
}

// ------------------------------------------------------------------ HTTP / SSE

static int dial(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0)
    return -1;
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

// One request on its own connection, read to EOF. Returns the body.
static std::string request(int port, const char* method, const std::string& path, const std::string& body)
{
    int fd = -1;
    for(int tries=0; tries<100 && fd<0; tries++)
    {
        fd = dial(port);
        if(fd<0)
        usleep(50000);
    }
    if(fd<0)
    return "";
    char head[512];
    std::snprintf(head, sizeof head,
                  "%s %s HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n"
                  "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                  method, path.c_str(), body.size());
    std::string out = std::string(head) + body;
    if(write(fd, out.data(), out.size())<0) {}
    std::string in;
    char chunk[4096];
    for(;;)
    {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if(n<=0)
        break;
        in.append(chunk, n);
    }
    close(fd);
    size_t split = in.find("\r\n\r\n");
    return split==std::string::npos ? "" : in.substr(split+4);
}

// The SSE stream the page holds open. Also what keeps the server from calling
// the session abandoned and switching the analysis off after 5 s.
class Stream
{
    public:
    bool open(int port)
    {
        for(int tries=0; tries<100 && fd<0; tries++)
        {
            fd = dial(port);
            if(fd<0)
            usleep(50000);
        }
        if(fd<0)
        return false;
        std::string req = "GET /api/events?id=main HTTP/1.1\r\nHost: localhost\r\n\r\n";
        if(write(fd, req.data(), req.size())<0) {}
        return true;
    }

    ~Stream() { if(fd>=0) close(fd); }

    // Next pushed state before the deadline, with the moment it arrived.
    bool next(long long deadline, std::string& state, long long& at)
    {
        for(;;)
        {
            size_t end = buffer.find("\n\n");
            while(end!=std::string::npos)
            {
                std::string frame = buffer.substr(0, end);
                buffer.erase(0, end+2);
                size_t data = frame.find("data: ");
                if(data!=std::string::npos)
                {
                    state = frame.substr(data+6);
                    at = arrived;
                    return true;
                }
                end = buffer.find("\n\n");   // a keep-alive comment: read on
            }
            long long left = deadline-now_ms();
            if(left<=0)
            return false;
            pollfd p = {fd, POLLIN, 0};
            if(poll(&p, 1, (int)left)<=0)
            continue;
            char chunk[65536];
            ssize_t n = read(fd, chunk, sizeof chunk);
            if(n<=0)
            return false;
            arrived = now_ms();
            buffer.append(chunk, n);
        }
    }

    void drain()
    {
        std::string state;
        long long at;
        while(next(now_ms(), state, at)) {}
    }

    private:
    int fd = -1;
    std::string buffer;
    long long arrived = 0;
};

// ------------------------------------------------------------------ the server

static pid_t server_pid = -1;

static int start_server(const std::string& fen)
{
    int out[2];
    if(pipe(out)!=0)
    return 0;
    server_pid = fork();
    if(server_pid==0)
    {
        dup2(out[1], 1);
        close(out[0]);
        close(out[1]);
        // port=0 is not a thing here: the server tries 8173 and the next 20, so
        // a port of our own keeps it away from one a browser may be using.
        execl("./gui/ascaniusfish_gui", "./gui/ascaniusfish_gui", "port=8290",
              ("fen=" + fen).c_str(), (char*)nullptr);
        _exit(127);
    }
    close(out[1]);
    // It prints "... http://localhost:<port> ..." once it is listening.
    std::string line;
    char c;
    while(read(out[0], &c, 1)==1 && c!='\n')
    line += c;
    close(out[0]);
    size_t at = line.find("localhost:");
    if(at==std::string::npos)
    {
        std::printf("the server did not start: %s\n", line.c_str());
        return 0;
    }
    return std::atoi(line.c_str()+at+10);
}

static void stop_server()
{
    if(server_pid>0)
    {
        kill(server_pid, SIGTERM);   // main() aborts the searches and quits the engines
        waitpid(server_pid, nullptr, 0);
        server_pid = -1;
    }
}

int main(int argc, char** argv)
{
    std::signal(SIGPIPE, SIG_IGN);
    long long soak_ms = argc>1 ? std::atoll(argv[1]) : DEFAULT_SOAK_MS;
    int plies = argc>2 ? std::atoi(argv[2]) : DEFAULT_PLIES;
    std::string fen = argc>3 ? argv[3] : PROBE_FEN;

    int port = start_server(fen);
    if(!port)
    return 1;
    Stream stream;
    if(!stream.open(port))
    {
        std::printf("no SSE stream on port %d\n", port);
        stop_server();
        return 1;
    }
    std::printf("Analyse mode, %lld ms per position, %d plies down the engine's own line.\n\n",
                soak_ms, plies);
    request(port, "POST", "/api/analyse", "{\"id\":\"main\",\"on\":true}");

    bool numbered = true, seq_order_ok = true;
    int played = 0;
    for(int ply=0; ply<plies; ply++)
    {
        long long until = now_ms()+soak_ms;
        while(now_ms()<until)
        stream.drain(), usleep(20000);

        std::string before = request(port, "GET", "/api/state?id=main", "");
        std::string move = analysis_first_move(before);
        int soaked = analysis_depth(before);
        if(move.empty())
        {
            std::printf("no line to follow after the soak - is ./ascaniusfish_uci built?\n");
            break;
        }
        stream.drain();

        // Drag the engine's own suggestion onto the board.
        long long t0 = now_ms();
        std::string answer = request(port, "POST", "/api/move",
                                    "{\"id\":\"main\",\"uci\":\"" + move + "\"}");
        long long t_answer = now_ms();
        if(state_seq(answer)<0)
        numbered = false;

        // Every state pushed while the browser is still busy with the answer.
        // These are states the page has drawn by the time the answer lands.
        std::vector<std::string> pushed;
        std::vector<long long> pushed_at;
        std::string state;
        long long at;
        while(stream.next(t_answer+JSON_TURN_MS, state, at))
        {
            pushed.push_back(state);
            pushed_at.push_back(at);
        }

        // Two verdicts on the same states. Arrival order applies the answer
        // last because it is the last to be turned into an object; seq order
        // keeps whichever state the server emitted later.
        const std::string* by_arrival = &answer;
        const std::string* by_seq = &answer;
        for(size_t i=0;i<pushed.size();i++)
        if(state_seq(pushed[i])>state_seq(*by_seq))
        by_seq = &pushed[i];

        int arrival_depth = analysis_depth(*by_arrival);
        int seq_depth = analysis_depth(*by_seq);
        std::printf("ply %d  soaked to depth %2d, dragged %s\n", ply+1, soaked, move.c_str());
        std::printf("  answer at %+4lld ms (seq %lld, no line), %d state%s pushed by %+4lld ms"
                    " (newest seq %lld, depth %d)\n",
                    t_answer-t0, state_seq(answer), (int)pushed.size(),
                    pushed.size()==1 ? "" : "s",
                    pushed.empty() ? 0 : pushed_at.back()-t0, state_seq(*by_seq), seq_depth);

        // What the panel says once the page has applied everything it has.
        if(arrival_depth<0)
        {
            // The old page is now empty. How long before it is told again?
            long long deadline = now_ms()+NEXT_LINE_WAIT_MS;
            long long filled = -1;
            while(stream.next(deadline, state, at))
            if(analysis_depth(state)>=0)
            {
                filled = at;
                break;
            }
            if(filled<0)
            std::printf("  arrival order: \"Starting the search...\" still, %lld s later\n",
                        NEXT_LINE_WAIT_MS/1000);
            else
            std::printf("  arrival order: \"Starting the search...\" for %.2f s, until depth %d\n",
                        (filled-t0)/1000.0, analysis_depth(state));
        }
        else
        std::printf("  arrival order: depth %d - the answer happened to be the newer state\n",
                    arrival_depth);

        if(seq_depth<0)
        {
            seq_order_ok = false;
            std::printf("  seq order:     no line either - FAIL\n");
        }
        else
        std::printf("  seq order:     depth %d, at %+lld ms\n", seq_depth,
                    (by_seq==&answer ? t_answer : pushed_at[by_seq-&pushed[0]])-t0);
        std::printf("\n");
        played++;
    }

    stop_server();
    if(!numbered)
    {
        std::printf("FAIL: a state came back without a \"seq\" - the page cannot order two of them\n");
        return 1;
    }
    if(!played)
    {
        std::printf("FAIL: no ply was played\n");
        return 1;
    }
    std::printf("%s\n", seq_order_ok
        ? "PASS: in seq order the line the server had already pushed is the one that stands."
        : "FAIL: seq order still ended up on a state with no line.");
    return seq_order_ok ? 0 : 1;
}
