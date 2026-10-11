// OWNERSHIP=Claude
// The GUI's password login and load limits (#109), in two parts.
//
// 1. gui/sha256.hpp against the standard vectors (FIPS 180-4 examples, RFC
//    4231 HMAC cases), and gui/access.hpp on its own: cookie signing and
//    expiry, the password check, the failed-login limits with a fake clock,
//    the client address behind a proxy, and the post-login redirect target.
// 2. The real server, started with a password and small limits: what a
//    visitor without the cookie gets, login and its rate limits, the cookie
//    after a restart, the board, engine and connection caps, the request
//    timeout, and the SSE keep-alive on an idle board while another one is
//    busy. Also that the server's output never shows the password or a token.
//
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
//       -DNDEBUG -o diagnostics/gui_access_test diagnostics/gui_access_test.cpp
//
// Run it from the repo root after `make gui/ascaniusfish_gui ascaniusfish_uci`.
// Part 2 takes about 40 s (the timeouts it checks are 10 and 15 s). Exit code
// 1 on any failure; `./diagnostics/gui_access_test unit` runs part 1 only.
#include "../gui/access.hpp"

#include <arpa/inet.h>
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
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if(!ok)
    failures++;
}

// ------------------------------------------------------------------ part 1

static std::string from_hex(const std::string& h)
{
    std::string out;
    for(size_t i=0;i+1<h.size();i+=2)
    out += (char)std::stoi(h.substr(i, 2), nullptr, 16);
    return out;
}

static void test_sha256()
{
    using sha256::hex;
    using sha256::digest;
    check(hex(digest("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of \"\"");
    check(hex(digest("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of \"abc\"");
    check(hex(digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))
          == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256, 448 bits (two blocks)");
    check(hex(digest("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"))
          == "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1", "SHA-256, 896 bits");
    sha256::Hasher h;
    std::string chunk(1000, 'a');
    for(int i=0;i<1000;i++)
    h.update(chunk);
    check(hex(h.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of a million 'a', in pieces");

    // RFC 4231 test cases 1-4, 6 and 7 (5 is a truncated output).
    struct Case { std::string key, data, mac; const char* name; };
    std::vector<Case> cases = {
        {std::string(20, '\x0b'), "Hi There", "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "HMAC RFC 4231 case 1"},
        {"Jefe", "what do ya want for nothing?", "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", "HMAC RFC 4231 case 2"},
        {std::string(20, '\xaa'), std::string(50, '\xdd'), "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe", "HMAC RFC 4231 case 3"},
        {from_hex("0102030405060708090a0b0c0d0e0f10111213141516171819"), std::string(50, '\xcd'),
         "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b", "HMAC RFC 4231 case 4"},
        {std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First",
         "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", "HMAC RFC 4231 case 6 (long key)"},
        {std::string(131, '\xaa'), "This is a test using a larger than block-size key and a larger than block-size data. "
                                   "The key needs to be hashed before being used by the HMAC algorithm.",
         "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2", "HMAC RFC 4231 case 7"},
    };
    for(const Case& c : cases)
    check(hex(sha256::hmac(c.key, c.data)) == c.mac, c.name);
}

static void test_login()
{
    using namespace gui_access;
    Login login;
    std::string error;
    check(!login.setup("pw", "short", error) && !login.on(), "a short secret is refused");
    check(!login.setup("", "0123456789abcdef0123", error), "an empty password is refused");
    check(login.setup("correct horse", "0123456789abcdef0123", error) && login.on(), "setup with password and secret");
    check(login.password_ok("correct horse"), "the password is accepted");
    check(!login.password_ok("correct hors") && !login.password_ok("correct horse ") && !login.password_ok(""),
          "a near miss is not");

    long long now = 1800000000;
    std::string cookie = login.make_cookie(now);
    check(login.cookie_ok(cookie, now), "a fresh cookie is good");
    check(login.cookie_ok(cookie, now+COOKIE_SECONDS-1), "still good a second before 30 days");
    check(!login.cookie_ok(cookie, now+COOKIE_SECONDS), "expired after 30 days");
    std::string tampered = cookie;
    tampered.back() = tampered.back()=='0' ? '1' : '0';
    check(!login.cookie_ok(tampered, now), "a changed signature is refused");
    std::string later = std::to_string(now+10*COOKIE_SECONDS) + cookie.substr(cookie.find('.'));
    check(!login.cookie_ok(later, now), "a moved expiry is refused");
    check(!login.cookie_ok("", now) && !login.cookie_ok(".", now) && !login.cookie_ok("abc.def", now), "garbage is refused");

    Login other_secret, other_password;
    other_secret.setup("correct horse", "another secret, long enough", error);
    other_password.setup("new password", "0123456789abcdef0123", error);
    check(!other_secret.cookie_ok(cookie, now), "a new secret logs everyone out");
    check(!other_password.cookie_ok(cookie, now), "a new password logs everyone out");
    Login same;
    same.setup("correct horse", "0123456789abcdef0123", error);
    check(same.cookie_ok(cookie, now), "the same secret and password (a restart) keep it");

    check(cookie_value("a=1; gui_login=xyz; b=2", "gui_login")=="xyz", "cookie_value finds its cookie");
    check(cookie_value("xgui_login=1; gui_login_old=2", "gui_login")=="", "cookie_value matches the whole name");
}

static void test_limiter()
{
    using namespace gui_access;
    Login_Limiter limiter;
    limiter.per_ip = 10;
    limiter.total = 25;
    bool whole = false;
    long long t = 1000000;
    bool allowed = true;
    for(int i=0;i<10;i++)
    {
        allowed = allowed && limiter.wait_ms("1.1.1.1", t, whole)==0;
        limiter.failed("1.1.1.1", t);
        t += 1000;
    }
    check(allowed, "tries 1-10 from one address are allowed");
    long long wait = limiter.wait_ms("1.1.1.1", t, whole);
    check(wait>0 && !whole, "the 11th try from it waits");
    check(limiter.wait_ms("2.2.2.2", t, whole)==0, "another address is not affected");
    check(limiter.wait_ms("1.1.1.1", 1000000+LOGIN_WINDOW_MS, whole)==0, "the first failure leaves the window after 5 min");
    t = 1000000+LOGIN_WINDOW_MS;
    for(int i=0;i<15;i++)
    limiter.failed("3.3.3." + std::to_string(i), t);   // 9 + 15 = 24 inside the window
    check(limiter.wait_ms("4.4.4.4", t, whole)==0, "24 failures in all: still allowed");
    limiter.failed("4.4.4.4", t);
    check(limiter.wait_ms("5.5.5.5", t, whole)>0 && whole, "25 in all: everyone waits");
    check(limiter.addresses()<=25, "the table holds no more addresses than failures in the window");
}

static void test_client()
{
    using namespace gui_access;
    std::map<std::string, std::string> h = {{"x-real-ip", "203.0.113.7"}, {"x-forwarded-proto", "https"}};
    Client c = client_of("172.17.0.1", h, true);
    check(c.ip=="203.0.113.7" && c.https, "behind a private proxy: X-Real-IP and https");
    c = client_of("172.17.0.1", h, false);
    check(c.ip=="172.17.0.1" && !c.https, "without GUI_TRUST_PROXY the headers are ignored");
    c = client_of("198.51.100.9", h, true);
    check(c.ip=="198.51.100.9" && !c.https, "from a public peer the headers are ignored");
    std::map<std::string, std::string> f = {{"x-forwarded-for", "1.2.3.4, 203.0.113.8"}};
    check(client_of("127.0.0.1", f, true).ip=="203.0.113.8", "X-Forwarded-For: the last entry, not the forgeable first");
    std::map<std::string, std::string> bad = {{"x-real-ip", "not an address"}};
    check(client_of("10.1.2.3", bad, true).ip=="10.1.2.3", "an invalid X-Real-IP is ignored");
    check(private_ipv4("127.0.0.1") && private_ipv4("10.0.0.1") && private_ipv4("172.31.255.1") && private_ipv4("192.168.1.1")
          && !private_ipv4("172.32.0.1") && !private_ipv4("8.8.8.8"), "private address ranges");

    check(safe_next("/?id=abc")=="/?id=abc", "safe_next keeps a board link");
    check(safe_next("//evil.example")=="/" && safe_next("/\\evil.example")=="/" && safe_next("https://x")=="/"
          && safe_next("/a\r\nSet-Cookie: x")=="/" && safe_next("/login")=="/", "safe_next refuses other sites and header breaks");
}

// ------------------------------------------------------------------ part 2

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

// Whatever arrives on fd within timeout_ms, until the server closes it or
// `until` shows up.
static std::string read_some(int fd, int timeout_ms, const std::string& until = "")
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
        if(!until.empty() && got.find(until)!=std::string::npos)
        break;
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

static const char* PASSWORD = "test password 109";
static const char* SECRET = "test-secret-for-the-diagnostic-0123";
static const char* LOG = "/tmp/gui_access_test.log";

static pid_t server_pid = -1;

// Starts the server with the test's password and limits; its port, or 0.
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
        setenv("GUI_TRUST_PROXY", "1", 1);
        setenv("GUI_MAX_CONNECTIONS", "6", 1);
        setenv("GUI_MAX_ENGINES", "1", 1);
        setenv("GUI_MAX_BOARDS", "3", 1);
        setenv("GUI_LOGIN_FAILS_TOTAL", "15", 1);
        execl("./gui/ascaniusfish_gui", "ascaniusfish_gui", "port=8391", "ptt=none", "engine=./ascaniusfish_uci", (char*)nullptr);
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

static std::string login_body(const std::string& password)
{
    return "password=" + gui_access::url_encode(password) + "&next=%2F%3Fid%3Dmain";
}

static void test_server()
{
    int port = start_server();
    check(port>0, "the server starts with a password");
    if(!port)
    return;

    // Without the cookie.
    Reply r = request(port, "GET", "/");
    check(r.status==303 && r.header("Location")=="/login", "GET / goes to the login");
    r = request(port, "GET", "/?id=x1");
    check(r.status==303 && r.header("Location")=="/login?next=%2F%3Fid%3Dx1", "a board link comes back after the login");
    r = request(port, "GET", "/api/state?id=x2");
    check(r.status==401 && r.body.find("not logged in")!=std::string::npos, "/api/ answers 401");
    r = request(port, "POST", "/api/analyse", "", "{\"id\":\"x3\",\"on\":\"true\"}");
    check(r.status==401, "a command answers 401");
    r = request(port, "GET", "/api/events?id=x4");
    check(r.status==401, "the event stream answers 401");
    r = request(port, "GET", "/index.html?token=abc");
    check(r.status==303, "?token= opens nothing");
    r = request(port, "GET", "/login");
    check(r.status==200 && r.body.find("name=\"password\"")!=std::string::npos, "the login page");
    r = request(port, "GET", "/favicon.png");
    check(r.status==200, "the icon needs no login");

    // Login and its limits. The test connects from 127.0.0.1, a private peer,
    // so with GUI_TRUST_PROXY=1 X-Real-IP / X-Forwarded-For name the client.
    for(int i=0;i<10;i++)
    {
        r = request(port, "POST", "/login", "X-Real-IP: 198.51.100.1\r\n", login_body("wrong"));
        if(r.status!=401)
        break;
    }
    check(r.status==401 && r.body.find("Wrong password")!=std::string::npos, "10 wrong passwords: 401 each");
    r = request(port, "POST", "/login", "X-Real-IP: 198.51.100.1\r\n", login_body(PASSWORD));
    check(r.status==429 && r.body.find("from your address")!=std::string::npos && r.header("Set-Cookie").empty(),
          "the 11th try waits, even with the right password");
    r = request(port, "POST", "/login", "X-Forwarded-For: 203.0.113.50, 198.51.100.1\r\n", login_body(PASSWORD));
    check(r.status==429, "a forged first X-Forwarded-For entry does not get around it");
    r = request(port, "POST", "/login", "X-Real-IP: 198.51.100.2\r\nX-Forwarded-Proto: https\r\n", login_body(PASSWORD));
    std::string set_cookie = r.header("Set-Cookie");
    check(r.status==303 && r.header("Location")=="/?id=main", "the right password from another address: back to the board");
    check(set_cookie.find("gui_login=")==0 && set_cookie.find("HttpOnly")!=std::string::npos
          && set_cookie.find("SameSite=Lax")!=std::string::npos && set_cookie.find("Max-Age=2592000")!=std::string::npos
          && set_cookie.find("Secure")!=std::string::npos, "the cookie: HttpOnly, SameSite=Lax, 30 days, Secure over https");
    check(r.header("Strict-Transport-Security").find("max-age=")==0, "HSTS over https");
    std::string cookie = "Cookie: " + set_cookie.substr(0, set_cookie.find(';')) + "\r\n";
    r = request(port, "POST", "/login", "X-Real-IP: 198.51.100.3\r\n", login_body(PASSWORD));
    check(r.header("Set-Cookie").find("Secure")==std::string::npos, "no Secure over plain http");
    check(r.header("X-Content-Type-Options")=="nosniff" && r.header("X-Frame-Options")=="DENY"
          && r.header("Referrer-Policy")=="same-origin", "the browser protection headers (#111)");
    check(r.header("Strict-Transport-Security").empty(), "no HSTS over plain http");

    r = request(port, "GET", "/api/state?id=main", cookie);
    check(r.status==200 && r.body.find("\"mode\"")!=std::string::npos, "the cookie opens the API");
    r = request(port, "GET", "/api/server", cookie);
    check(r.body=="{\"login\":true}", "/api/server says there is a login");
    r = request(port, "GET", "/login", cookie);
    check(r.status==303, "the login page sends a logged-in visitor on");
    std::string bad = cookie;
    bad[bad.size()-3] = bad[bad.size()-3]=='a' ? 'b' : 'a';
    r = request(port, "GET", "/api/state", bad);
    check(r.status==401, "a changed cookie is refused");
    gui_access::Login login;
    std::string error;
    login.setup(PASSWORD, SECRET, error);
    r = request(port, "GET", "/api/state", "Cookie: gui_login=" + login.make_cookie(time(nullptr)-gui_access::COOKIE_SECONDS-60) + "\r\n");
    check(r.status==401, "an expired cookie is refused");
    r = request(port, "POST", "/logout", cookie);
    check(r.status==303 && r.header("Location")=="/login" && r.header("Set-Cookie").find("Max-Age=0")!=std::string::npos,
          "logout removes the cookie");

    for(int i=0;i<4;i++)   // 10 + 4 failures; one more makes the 15 of GUI_LOGIN_FAILS_TOTAL
    request(port, "POST", "/login", "X-Real-IP: 198.51.100." + std::to_string(10+i) + "\r\n", login_body("wrong"));
    r = request(port, "POST", "/login", "X-Real-IP: 198.51.100.20\r\n", login_body("wrong"));
    check(r.status==401, "the 15th failure in all is still a wrong password");
    r = request(port, "POST", "/login", "X-Real-IP: 198.51.100.21\r\n", login_body(PASSWORD));
    check(r.status==429 && r.body.find("on this server")!=std::string::npos, "then the whole server waits");

    // A restart keeps the login.
    stop_server();
    std::ifstream log_file(LOG);
    std::stringstream log;
    log << log_file.rdbuf();
    check(log.str().find(PASSWORD)==std::string::npos && log.str().find("token")==std::string::npos
          && log.str().find("Token")==std::string::npos, "the output shows neither the password nor a token");
    port = start_server();
    r = request(port, "GET", "/api/state?id=main", cookie);
    check(r.status==200, "after a restart the cookie still works");

    // Boards: "main" and the two below make 3; x1-x4 above made none.
    check(request(port, "GET", "/api/state?id=b1", cookie).status==200, "board 2 of 3");
    check(request(port, "GET", "/api/state?id=b2", cookie).status==200, "board 3 of 3");
    r = request(port, "GET", "/api/state?id=b3", cookie);
    check(r.status==503 && r.body.find("maximum of 3 boards")!=std::string::npos, "board 4 is refused with a message");
    r = request(port, "POST", "/api/analyse", cookie, "{\"id\":\"b4\",\"on\":\"true\"}");
    check(r.status==503, "and a command cannot make it either");
    check(request(port, "GET", "/api/state?id=b1", cookie).status==200, "an existing board still opens");

    // Engines: one may run. b1 analyses; b2 asking for analysis is told why not.
    // b1 has a page open (a stream, drained in the background), or the server
    // would stop its analysis 5 s after nobody watches.
    int busy_stream = connect_to(port);
    std::string sub1 = "GET /api/events?id=b1 HTTP/1.1\r\nHost: localhost\r\n" + cookie + "\r\n";
    if(write(busy_stream, sub1.data(), sub1.size())<0) {}
    read_some(busy_stream, 2000, "event: state");
    volatile bool draining = true;
    std::thread drain([&] { while(draining) read_some(busy_stream, 200); });
    r = request(port, "POST", "/api/analyse", cookie, "{\"id\":\"b1\",\"on\":\"true\"}");
    check(r.status==200 && r.body.find("in use on other boards")==std::string::npos, "the first engine starts");
    r = request(port, "POST", "/api/analyse", cookie, "{\"id\":\"b2\",\"on\":\"true\"}");
    check(r.status==200 && r.body.find("all 1 engines of this server are in use")!=std::string::npos,
          "the second is refused with a message");

    // Keep-alive: b2's stream is silent while b1's analysis keeps the server
    // busy, and must still be sent a comment within KEEPALIVE_MS plus a poll.
    int stream = connect_to(port);
    std::string sub = "GET /api/events?id=b2 HTTP/1.1\r\nHost: localhost\r\n" + cookie + "\r\n";
    if(write(stream, sub.data(), sub.size())<0) {}
    std::string first = read_some(stream, 2000, "event: state");
    check(first.find("text/event-stream")!=std::string::npos, "b2's event stream opens");

    // Connections: the two streams and four idle sockets make the 6.
    std::vector<int> idle;
    for(int i=0;i<4;i++)
    idle.push_back(connect_to(port));
    usleep(300000);
    r = request(port, "GET", "/api/server", cookie);
    check(r.status==503 && r.body.find("too many open connections")!=std::string::npos, "connection 7 of 6 is refused");

    // Idle sockets that never send a request are answered 408 and closed.
    std::string timed_out = read_some(idle[0], 12000);
    check(timed_out.find("408")!=std::string::npos, "a connection that sends nothing is closed after 10 s");
    for(int fd : idle)
    close(fd);
    usleep(300000);
    check(request(port, "GET", "/api/server", cookie).status==200, "and the room is free again");

    std::string later = read_some(stream, 12000, ": keep-alive");
    check(later.find(": keep-alive")!=std::string::npos, "the idle stream gets its keep-alive while another board is busy");
    close(stream);
    draining = false;
    drain.join();
    close(busy_stream);

    request(port, "POST", "/api/analyse", cookie, "{\"id\":\"b1\",\"on\":\"false\"}");
    stop_server();
}

int main(int argc, char** argv)
{
    test_sha256();
    test_login();
    test_limiter();
    test_client();
    if(argc<2 || std::string(argv[1])!="unit")
    test_server();
    std::printf("%s: %d failure%s\n", failures ? "FAILED" : "passed", failures, failures==1 ? "" : "s");
    return failures ? 1 : 0;
}
