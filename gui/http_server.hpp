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
// Only 127.0.0.1 is bound: the server reads files from disk and takes commands
// without authentication, so it must not be reachable from the network.
#ifndef GUI_HTTP_SERVER_HPP
#define GUI_HTTP_SERVER_HPP
#include <algorithm>
#include <cerrno>
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

struct Request
{
    std::string method;
    std::string path;   // percent-decoded, without the query string
    std::string query;  // raw, after '?'
    std::string body;

    // Value of a query parameter, or "" if it isn't there.
    std::string param(const std::string& name) const
    {
        for(size_t i=0;i<query.size();)
        {
            size_t end = std::min(query.find('&', i), query.size());
            size_t eq = query.find('=', i);
            if(eq<end && query.compare(i, eq-i, name)==0)
            return decode(query.substr(eq+1, end-eq-1));
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
    std::string topic;     // which stream, when sse

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
};

inline const char* status_text(int status)
{
    switch(status)
    {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
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

    // Binds the first free port in [first_port, first_port+tries).
    bool listen_on(int first_port, int tries = 20)
    {
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
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
    void publish(const std::string& topic, const std::string& event, const std::string& data)
    {
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

    int subscribers(const std::string& topic) const
    {
        int n = 0;
        for(const Client& c : clients)
        n += c.sse && c.topic==topic;
        return n;
    }

    // Set from a signal handler to end run(), so Ctrl-C leaves through main()
    // and the engine processes are told to quit rather than orphaned.
    volatile sig_atomic_t stopping = 0;

    void run()
    {
        while(!stopping)
        poll_once(30000);
    }

    // One poll iteration: accepts, reads, answers, writes. Also sends an SSE
    // comment to every stream on a quiet tick, which is how a page that went
    // away (closed tab) gets noticed and dropped.
    void poll_once(int timeout_ms)
    {
        std::vector<pollfd> fds;
        fds.push_back({listen_fd, POLLIN, 0});
        fds.push_back({wake_fds[0], POLLIN, 0});
        for(Client& c : clients)
        fds.push_back({c.fd, (short)(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});

        int ready = poll(fds.data(), fds.size(), timeout_ms);
        if(ready==0)
        {
            for(Client& c : clients)
            if(c.sse)
            queue(c, ": keep-alive\n\n");
        }
        else if(ready<0)
        return;
        else
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
        if(on_tick)
        on_tick();
        flush_all();
    }

    private:
    struct Client
    {
        int fd = -1;
        std::string in, out;
        bool sse = false;
        std::string topic;
        bool done = false;            // close once `out` is drained
        size_t content_length = 0;
        size_t header_end = 0;        // 0 until the blank line arrived
    };

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
            int fd = accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if(fd<0)
            return;
            Client c;
            c.fd = fd;
            clients.push_back(c);
        }
    }

    void queue(Client& c, const std::string& data)
    {
        c.out += data;
    }

    void on_readable(Client& c)
    {
        char chunk[8192];
        for(;;)
        {
            ssize_t n = read(c.fd, chunk, sizeof chunk);
            if(n>0)
            {
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
        if(c.sse)
        return;  // an established stream sends nothing back
        if(!c.header_end)
        {
            size_t blank = c.in.find("\r\n\r\n");
            size_t skip = 4;
            if(blank==std::string::npos)
            {
                blank = c.in.find("\n\n");
                skip = 2;
            }
            if(blank==std::string::npos)
            return;
            c.header_end = blank+skip;
            c.content_length = 0;
            std::string head = c.in.substr(0, blank);
            for(size_t i=0;i<head.size();)
            {
                size_t nl = std::min(head.find('\n', i), head.size());
                std::string line = head.substr(i, nl-i);
                i = nl+1;
                size_t colon = line.find(':');
                if(colon==std::string::npos)
                continue;
                std::string name = line.substr(0, colon);
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
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
        c.in.erase(0, c.header_end+c.content_length);
        c.header_end = 0;
        c.content_length = 0;

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
            flush(c);
            if(on_subscribe)
            on_subscribe(c.topic);   // may publish(), which queues onto this client
            return;
        }
        respond(c, res);
        c.done = true;  // Connection: close
    }

    void respond(Client& c, const Response& res)
    {
        char head[512];
        int n = snprintf(head, sizeof head,
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n"
            "\r\n",
            res.status, status_text(res.status), res.content_type.c_str(), res.body.size());
        queue(c, std::string(head, n));
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
                continue;
            }
            if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))
            return;
            c.done = true;   // EPIPE or similar: the page is gone
            c.out.clear();
            return;
        }
    }

    void flush_all()
    {
        for(Client& c : clients)
        flush(c);
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
