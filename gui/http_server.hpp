// OWNERSHIP=Claude
// The slice of HTTP the GUI needs, on plain sockets: GET a static file, POST a
// JSON command, and hold server-sent-event streams open so the server can push
// a new position to every page watching a game.
//
// One thread, one poll() over the listening socket and all clients, so an open
// SSE stream never blocks a request. Ordinary responses say "Connection: close"
// — there is no keep-alive parsing here, which costs nothing on localhost.
//
// Work that happens off this thread (a search, gui/engine_link.hpp) never
// touches a socket: it calls wake(), and the loop runs on_tick() to collect
// whatever finished. All writing stays on the one thread.
//
// A Cloudflare quick tunnel holds the start of an event stream back (the
// first ~256 KB, released in bursts seconds apart; a single state can wait
// until the stream ends), so a page behind one long-polls instead: GET /api/wait is held here until the
// next "state" published on its topic and answered as an ordinary response,
// which the tunnel passes on at once. The page sends its own id with each
// wait, so it still counts as watching (subscribers(), audience()) in the
// moment between one answer and its next wait.
//
// listen_on() binds 127.0.0.1 by default; a caller passing another address
// (gui_server.cpp's bind=) is exposing this on a LAN or behind a tunnel, and
// the token or password check gui_server.cpp does before touching a session is
// what makes that safe — this header itself knows nothing about auth, only that
// a Response may carry a Set-Cookie and a Location.
//
// On the internet (#109) the loop also has to survive its visitors: at most
// max_connections at once (one more is told 503 and closed), a request must
// arrive whole within REQUEST_TIMEOUT_MS, and a client that stops reading what
// it is sent is dropped after WRITE_TIMEOUT_MS or once MAX_PENDING_OUT piles
// up. An event stream is sent a comment after KEEPALIVE_MS of silence, so a
// proxy in front does not time it out while the page sits on a quiet board.
#ifndef GUI_HTTP_SERVER_HPP
#define GUI_HTTP_SERVER_HPP
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <map>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

constexpr size_t MAX_REQUEST_BYTES = 1 << 20;  // a FEN or a move; nothing here is big
constexpr long long WAIT_MAX_MS = 20000;      // a long poll is answered by then anyway, well inside Cloudflare's 100 s
constexpr long long POLLER_GRACE_MS = 3000;   // a long-polling page counts as there this long after its last wait
constexpr long long REQUEST_TIMEOUT_MS = 10000;  // from connect to the last byte of the request
constexpr long long WRITE_TIMEOUT_MS = 30000;    // pending output that the client does not take
constexpr size_t MAX_PENDING_OUT = 8 << 20;      // unsent bytes a slow stream may pile up
constexpr long long KEEPALIVE_MS = 15000;        // well inside a proxy's usual 60 s read timeout
constexpr int HOUSEKEEPING_MS = 5000;            // the longest poll(), so the timers above run
constexpr size_t DEFAULT_MAX_CONNECTIONS = 64;

struct Request
{
    std::string method;
    std::string path;   // percent-decoded, without the query string
    std::string query;  // raw, after '?'
    std::string body;
    std::map<std::string, std::string> headers;  // lowercased name -> value, e.g. "cookie"
    std::string peer;   // the connecting address (with a proxy in front, the proxy)

    // Value of a query parameter, or "" if it isn't there.
    std::string param(const std::string& name) const
    {
        return field(query, name);
    }

    // The same out of any "a=1&b=2" text, e.g. a form's body.
    static std::string field(const std::string& encoded, const std::string& name)
    {
        for(size_t i=0;i<encoded.size();)
        {
            size_t end = std::min(encoded.find('&', i), encoded.size());
            size_t eq = encoded.find('=', i);
            if(eq<end && eq-i==name.size() && encoded.compare(i, eq-i, name)==0)
            return decode(encoded.substr(eq+1, end-eq-1));
            i = end+1;
        }
        return "";
    }

    static std::string decode(const std::string& s)
    {
        std::string out;
        for(size_t i=0;i<s.size();i++)
        {
            if(s[i]=='+')
            out += ' ';
            else if(s[i]=='%' && i+2<s.size())
            {
                out += (char)strtol(s.substr(i+1, 2).c_str(), nullptr, 16);
                i += 2;
            }
            else
            out += s[i];
        }
        return out;
    }
};

struct Response
{
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    bool sse = false;      // keep the connection open as an event stream instead
    bool wait = false;     // a long poll: held until `topic` publishes a state past `after`
    std::string topic;     // which stream, when sse or wait
    long long after = 0;   // the last publish number the waiting page has
    std::string page;      // the waiting page's own id, so it stays counted between waits
    std::string set_cookie;  // Set-Cookie value, or "" for none (a plain response only)
    std::string location;    // Location, for a redirect (303)

    static Response json(const std::string& body, int status = 200)
    {
        Response r;
        r.status = status;
        r.body = body;
        return r;
    }

    static Response text(const std::string& body, int status = 200)
    {
        Response r = json(body, status);
        r.content_type = "text/plain; charset=utf-8";
        return r;
    }

    static Response file(const std::string& body, const std::string& type)
    {
        Response r;
        r.content_type = type;
        r.body = body;
        return r;
    }

    static Response event_stream(const std::string& topic)
    {
        Response r;
        r.sse = true;
        r.topic = topic;
        return r;
    }

    static Response long_poll(const std::string& topic, long long after, const std::string& page)
    {
        Response r;
        r.wait = true;
        r.topic = topic;
        r.after = after;
        r.page = page;
        return r;
    }
};

inline const char* status_text(int status)
{
    switch(status)
    {
        case 200: return "OK";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 503: return "Service Unavailable";
        default:  return "Internal Server Error";
    }
}

inline std::string mime_type(const std::string& path)
{
    size_t dot = path.rfind('.');
    std::string ext = dot==std::string::npos ? "" : path.substr(dot+1);
    if(ext=="html") return "text/html; charset=utf-8";
    if(ext=="js")   return "text/javascript; charset=utf-8";
    if(ext=="css")  return "text/css; charset=utf-8";
    if(ext=="json") return "application/json";
    if(ext=="svg")  return "image/svg+xml";
    if(ext=="png")  return "image/png";
    if(ext=="ico")  return "image/x-icon";
    if(ext=="woff2") return "font/woff2";
    if(ext=="mp3")  return "audio/mpeg";
    if(ext=="md")   return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

class Http_Server
{
    public:
    // Answers one request. An SSE response hands the connection to the server.
    std::function<Response(const Request&)> handler;
    // Called right after a page subscribes, so it can be sent the current state.
    std::function<void(const std::string& topic)> on_subscribe;
    // Called on every poll iteration, so a wake() from another thread turns
    // into work done here: this is where a finished search reaches the pages.
    std::function<void()> on_tick;
    // How long the next poll may block, asked fresh before each one. Unset
    // keeps the old fixed 30s; a clocked game running (gui_server.cpp) wants a
    // short one, or a flag falls only when something else happens to wake the
    // loop rather than within about as long as this returns.
    std::function<int()> poll_timeout;
    // Connections open at once, event streams and held waits included.
    size_t max_connections = DEFAULT_MAX_CONNECTIONS;

    Http_Server()
    {
        // Self-pipe: the only way another thread may reach this loop.
        if(pipe2(wake_fds, O_CLOEXEC | O_NONBLOCK)!=0)
        wake_fds[0] = wake_fds[1] = -1;
    }

    ~Http_Server()
    {
        for(Client& c : clients)
        close(c.fd);
        if(listen_fd>=0)
        close(listen_fd);
        for(int fd : wake_fds)
        if(fd>=0)
        close(fd);
    }

    // Wakes the poll loop from any thread. Nothing else here is thread-safe.
    void wake()
    {
        if(wake_fds[1]>=0 && write(wake_fds[1], "", 1)<0) {}  // a full pipe already means "wake up"
    }

    // Binds the first free port in [first_port, first_port+tries) on bind_ip
    // (an IPv4 address, e.g. "127.0.0.1" or "0.0.0.0"). False on a bad address
    // too, same as no free port.
    bool listen_on(int first_port, const std::string& bind_ip = "127.0.0.1", int tries = 20)
    {
        in_addr addr_in;
        if(inet_pton(AF_INET, bind_ip.c_str(), &addr_in)!=1)
        return false;
        // SOCK_CLOEXEC: an engine process started later (gui/engine_link.hpp)
        // must not inherit the listening socket and hold the port open.
        listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if(listen_fd<0)
        return false;
        int on = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        for(int p=first_port;p<first_port+tries;p++)
        {
            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_addr = addr_in;
            addr.sin_port = htons((uint16_t)p);
            if(bind(listen_fd, (sockaddr*)&addr, sizeof addr)==0 && ::listen(listen_fd, 16)==0)
            {
                bound_port = p;
                set_nonblocking(listen_fd);
                return true;
            }
        }
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    int port() const { return bound_port; }

    // Sends one event to every stream on `topic`.
    // A "state" is also kept as the topic's latest and answers its long polls.
    void publish(const std::string& topic, const std::string& event, const std::string& data)
    {
        if(event=="state")
        {
            Latest& l = latest[topic];
            l.n++;
            l.state = data;
            for(Client& c : clients)
            if(c.waiting && c.topic==topic)
            answer_wait(c);
        }
        std::string frame = "event: " + event + "\n";
        for(size_t i=0;i<data.size();)  // every line of the payload needs its own "data:"
        {
            size_t nl = std::min(data.find('\n', i), data.size());
            frame += "data: " + data.substr(i, nl-i) + "\n";
            i = nl+1;
        }
        frame += "\n";
        for(Client& c : clients)
        if(c.sse && c.topic==topic)
        queue(c, frame);
        flush_all();
    }

    // Sends one event to every stream, whatever its topic.
    void publish_all(const std::string& event, const std::string& data)
    {
        std::string frame = "event: " + event + "\ndata: " + data + "\n\n";
        for(Client& c : clients)
        if(c.sse && !c.done)
        queue(c, frame);
        flush_all();
    }

    // The pages watching right now (#68): every open stream, and how many of
    // them came from elsewhere — through a tunnel (cloudflared forwards from
    // localhost, but adds the visitor's address as a header) or from the LAN.
    struct Audience
    {
        int pages = 0;
        int remote = 0;
        bool operator==(const Audience& o) const { return pages==o.pages && remote==o.remote; }
    };

    Audience audience() const
    {
        Audience a;
        for(const Client& c : clients)
        if(c.sse && !c.done)
        {
            a.pages++;
            a.remote += c.remote;
        }
        for(const auto& entry : pollers)
        {
            a.pages++;
            a.remote += entry.second.remote;
        }
        return a;
    }

    // Every stream ever opened, so a page taking the place of one that closed
    // in the same tick still counts as a change.
    long long streams_opened = 0;

    int subscribers(const std::string& topic) const
    {
        int n = 0;
        for(const Client& c : clients)
        n += c.sse && c.topic==topic;
        for(const auto& entry : pollers)
        n += entry.second.topic==topic;
        return n;
    }

    // Set from a signal handler to end run(), so Ctrl-C leaves through main()
    // and the engine processes are told to quit rather than orphaned.
    volatile sig_atomic_t stopping = 0;

    void run()
    {
        while(!stopping)
        poll_once(std::min(poll_timeout ? poll_timeout() : 30000, HOUSEKEEPING_MS));
    }

    // One poll iteration: accepts, reads, answers, writes, and closes what
    // timed out. A stream silent for KEEPALIVE_MS gets an SSE comment, which is
    // also how a page that went away (closed tab) gets noticed and dropped.
    // That is per stream and by the clock, not on a quiet tick: a busy board
    // wakes the loop all the time, and the streams of the idle ones still
    // need their comment.
    void poll_once(int timeout_ms)
    {
        std::vector<pollfd> fds;
        fds.push_back({listen_fd, POLLIN, 0});
        fds.push_back({wake_fds[0], POLLIN, 0});
        for(Client& c : clients)
        fds.push_back({c.fd, (short)(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});

        int ready = poll(fds.data(), fds.size(), timeout_ms);
        if(ready<0)
        return;
        if(ready>0)
        {
            if(fds[0].revents & POLLIN)
            accept_all();
            if(fds[1].revents & POLLIN)
            drain_wake();
            for(size_t i=0;i<clients.size();i++)   // clients[i] lines up with fds[i+2]:
            {                                      // accept_all only appends
                if(i+2>=fds.size())
                break;
                short ev = fds[i+2].revents;
                if(ev & POLLIN)
                on_readable(clients[i]);
                if(ev & (POLLERR | POLLHUP | POLLNVAL))
                clients[i].done = true;
            }
        }
        expire_waits();
        if(on_tick)
        on_tick();
        keep_alive_and_expire();
        flush_all();
        reap();
    }

    private:
    struct Client
    {
        int fd = -1;
        std::string in, out;
        bool sse = false;
        std::string topic;
        bool loopback = true;         // the peer is 127.0.0.0/8
        bool remote = false;          // an SSE stream from elsewhere, see audience()
        bool waiting = false;         // a held long poll, see publish()
        long long waiting_since = 0;
        std::string page;             // the long-polling page's id
        bool done = false;           // close once `out` is drained
        bool answered = false;        // respond() ran: nothing more is read or handled
        std::string peer;             // dotted address
        long long opened = 0;         // connect time, for REQUEST_TIMEOUT_MS
        long long last_queued = 0;    // for KEEPALIVE_MS
        long long last_progress = 0;  // last time `out` shrank or was empty, for WRITE_TIMEOUT_MS
        size_t content_length = 0;
        size_t header_end = 0;        // 0 until the blank line arrived
        size_t scanned = 0;           // bytes of `in` already searched for it
        std::map<std::string, std::string> headers;  // filled once header_end is set
    };

    // The last "state" each topic published, and how many it has published:
    // a wait that arrives already behind is answered at once.
    struct Latest
    {
        long long n = 0;
        std::string state;
    };
    std::map<std::string, Latest> latest;

    // Long-polling pages by their id, while they are waiting or have been
    // within POLLER_GRACE_MS.
    struct Poller
    {
        std::string topic;
        bool remote = false;
        long long seen = 0;
    };
    std::map<std::string, Poller> pollers;

    static long long clock_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void answer_wait(Client& c)
    {
        const Latest& l = latest[c.topic];
        respond(c, Response::json("{\"n\":" + std::to_string(l.n) + ",\"state\":"
                                  + (l.state.empty() ? "null" : l.state) + "}"));
        c.waiting = false;
        c.done = true;  // Connection: close
        if(!c.page.empty() && pollers.count(c.page))
        pollers[c.page].seen = clock_ms();
    }

    // A wait held WAIT_MAX_MS is answered with what there is; a page that has
    // not come back within POLLER_GRACE_MS is gone.
    void expire_waits()
    {
        long long now = clock_ms();
        for(Client& c : clients)
        {
            if(c.waiting && now-c.waiting_since>=WAIT_MAX_MS)
            answer_wait(c);
            if(c.waiting && !c.done && !c.page.empty() && pollers.count(c.page))
            pollers[c.page].seen = now;
        }
        for(auto it=pollers.begin();it!=pollers.end();)
        if(now-it->second.seen>=POLLER_GRACE_MS)
        it = pollers.erase(it);
        else
        ++it;
    }

    int listen_fd = -1;
    int bound_port = 0;
    int wake_fds[2] = {-1, -1};   // [0] read, [1] written by wake()
    std::vector<Client> clients;

    void drain_wake()
    {
        char buf[64];
        while(read(wake_fds[0], buf, sizeof buf)>0)
        ;
    }

    static void set_nonblocking(int fd)
    {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    }

    void accept_all()
    {
        for(;;)
        {
            sockaddr_in peer{};
            socklen_t peer_len = sizeof peer;
            int fd = accept4(listen_fd, (sockaddr*)&peer, &peer_len, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if(fd<0)
            return;
            if(clients.size()>=max_connections)
            {
                // Best effort, no waiting: the reply fits any socket buffer.
                static const char busy[] =
                    "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain; charset=utf-8\r\n"
                    "Content-Length: 45\r\nConnection: close\r\n\r\n"
                    "too many open connections, try again shortly\n";
                if(send(fd, busy, sizeof busy-1, MSG_NOSIGNAL)<0) {}
                close(fd);
                continue;
            }
            Client c;
            c.fd = fd;
            c.loopback = peer.sin_family==AF_INET && (ntohl(peer.sin_addr.s_addr)>>24)==127;
            char text[INET_ADDRSTRLEN] = "";
            inet_ntop(AF_INET, &peer.sin_addr, text, sizeof text);
            c.peer = text;
            c.opened = c.last_queued = c.last_progress = clock_ms();
            clients.push_back(c);
        }
    }

    void queue(Client& c, const std::string& data)
    {
        if(c.out.empty())
        c.last_progress = clock_ms();   // the write timeout counts from here
        c.out += data;
        c.last_queued = clock_ms();
    }

    // Keep-alives for silent streams, and the end of whatever stalled: a
    // request still not whole after REQUEST_TIMEOUT_MS, or output the client
    // stopped taking.
    void keep_alive_and_expire()
    {
        long long now = clock_ms();
        for(Client& c : clients)
        {
            if(c.done && c.out.empty())
            continue;
            if(c.sse && !c.done && now-c.last_queued>=KEEPALIVE_MS)
            queue(c, ": keep-alive\n\n");
            if(!c.sse && !c.waiting && !c.done && now-c.opened>=REQUEST_TIMEOUT_MS)
            {
                respond(c, Response::text("request took too long", 408));
                c.done = true;
            }
            if(!c.out.empty() && (now-c.last_progress>=WRITE_TIMEOUT_MS || c.out.size()>MAX_PENDING_OUT))
            {
                c.out.clear();
                c.done = true;
            }
        }
    }

    void on_readable(Client& c)
    {
        char chunk[8192];
        for(;;)
        {
            ssize_t n = read(c.fd, chunk, sizeof chunk);
            if(n>0)
            {
                if(c.answered)
                continue;            // one request per connection: the rest is dropped
                c.in.append(chunk, n);
                if(c.in.size()>MAX_REQUEST_BYTES)
                {
                    respond(c, Response::text("request too large", 413));
                    c.done = true;
                    return;
                }
                continue;
            }
            if(n==0)                 // the page closed the connection
            c.done = true;
            break;                   // n<0: nothing more to read right now
        }
        if(!c.done || !c.in.empty())
        try_handle(c);
    }

    void try_handle(Client& c)
    {
        if(c.sse || c.waiting || c.answered)
        return;  // an established stream or a held wait sends nothing back
        if(!c.header_end)
        {
            // Only the new bytes, plus 3 for a blank line split across two
            // reads: a header dribbled in 200 bytes at a time would otherwise
            // be searched whole again after every read (#111).
            size_t from = c.scanned>3 ? c.scanned-3 : 0;
            c.scanned = c.in.size();
            size_t crlf = c.in.find("\r\n\r\n", from), lf = c.in.find("\n\n", from);
            size_t blank = std::min(crlf, lf);
            if(blank==std::string::npos)
            return;
            size_t skip = blank==crlf ? 4 : 2;
            c.header_end = blank+skip;
            c.content_length = 0;
            std::string head = c.in.substr(0, blank);
            for(size_t i=0;i<head.size();)
            {
                size_t nl = std::min(head.find('\n', i), head.size());
                std::string line = head.substr(i, nl-i);
                i = nl+1;
                if(!line.empty() && line.back()=='\r')
                line.pop_back();
                size_t colon = line.find(':');
                if(colon==std::string::npos)
                continue;
                std::string name = line.substr(0, colon);
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                size_t vstart = colon+1;
                while(vstart<line.size() && line[vstart]==' ')
                vstart++;
                c.headers[name] = line.substr(vstart);
                if(name=="content-length")
                c.content_length = (size_t)strtoul(line.c_str()+colon+1, nullptr, 10);
            }
        }
        if(c.in.size()<c.header_end+c.content_length)
        return;

        Request req;
        std::string line = c.in.substr(0, c.in.find('\n'));
        size_t sp1 = line.find(' '), sp2 = line.find(' ', sp1+1);
        if(sp1==std::string::npos || sp2==std::string::npos)
        {
            respond(c, Response::text("malformed request", 400));
            c.done = true;
            return;
        }
        req.method = line.substr(0, sp1);
        std::string target = line.substr(sp1+1, sp2-sp1-1);
        size_t q = target.find('?');
        if(q!=std::string::npos)
        {
            req.query = target.substr(q+1);
            target = target.substr(0, q);
        }
        req.path = Request::decode(target);
        req.body = c.in.substr(c.header_end, c.content_length);
        req.headers = c.headers;
        req.peer = c.peer;
        c.in.erase(0, c.header_end+c.content_length);
        c.header_end = 0;
        c.scanned = 0;
        c.content_length = 0;
        c.headers.clear();

        Response res = handler ? handler(req) : Response::text("no handler", 500);
        if(res.sse)
        {
            std::string head =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: keep-alive\r\n"
                "X-Accel-Buffering: no\r\n"
                "\r\n"
                "retry: 1000\n\n";
            queue(c, head);
            c.sse = true;
            c.topic = res.topic;
            c.remote = !c.loopback || req.headers.count("cf-connecting-ip") || req.headers.count("x-forwarded-for");
            streams_opened++;
            flush(c);
            if(on_subscribe)
            on_subscribe(c.topic);   // may publish(), which queues onto this client
            return;
        }
        if(res.wait)
        {
            c.topic = res.topic;
            c.page = res.page;
            if(!c.page.empty())
            {
                if(!pollers.count(c.page))
                streams_opened++;
                Poller& p = pollers[c.page];
                p.topic = res.topic;
                p.remote = !c.loopback || req.headers.count("cf-connecting-ip") || req.headers.count("x-forwarded-for");
                p.seen = clock_ms();
            }
            if(latest[res.topic].n>res.after)
            answer_wait(c);
            else
            {
                c.waiting = true;
                c.waiting_since = clock_ms();
            }
            return;
        }
        respond(c, res);
        c.done = true;  // Connection: close
    }

    void respond(Client& c, const Response& res)
    {
        c.answered = true;
        c.in.clear();
        std::string head = "HTTP/1.1 " + std::to_string(res.status) + " " + status_text(res.status) + "\r\n"
            "Content-Type: " + res.content_type + "\r\n"
            "Content-Length: " + std::to_string(res.body.size()) + "\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n";
        if(!res.set_cookie.empty())
        head += "Set-Cookie: " + res.set_cookie + "\r\n";
        if(!res.location.empty())
        head += "Location: " + res.location + "\r\n";
        queue(c, head + "\r\n");
        queue(c, res.body);
    }

    // Writes what it can; a client whose peer is gone is marked done.
    void flush(Client& c)
    {
        while(!c.out.empty())
        {
            ssize_t n = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);  // no SIGPIPE when a page is gone
            if(n>0)
            {
                c.out.erase(0, n);
                c.last_progress = clock_ms();
                continue;
            }
            if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))
            return;
            c.done = true;   // EPIPE or similar: the page is gone
            c.out.clear();
            return;
        }
    }

    // Only writes: publish() runs inside a request's handler, whose Client&
    // a reap() would move out from under it.
    void flush_all()
    {
        for(Client& c : clients)
        flush(c);
    }

    // Closes and drops the finished clients; only at the end of poll_once().
    void reap()
    {
        for(size_t i=0;i<clients.size();)
        {
            if(clients[i].done && clients[i].out.empty())
            {
                close(clients[i].fd);
                clients.erase(clients.begin()+i);
            }
            else
            i++;
        }
    }
};

#endif // GUI_HTTP_SERVER_HPP
