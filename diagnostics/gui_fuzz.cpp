// OWNERSHIP=Claude
// What a remote user can feed the GUI server, fed to it on purpose (#111):
// the parsers under AddressSanitizer and UBSan with random and mutated input,
// the hostile inputs the security review names one by one, and the HTTP loop
// itself on a loopback port.
//
//   make gui-fuzz                       builds it with the sanitizers and runs all three
//   ./diagnostics/gui_fuzz cases        each named hostile input: what it does, how long, how much
//   ./diagnostics/gui_fuzz case <name>  one of them (a crashing one ends the run)
//   ./diagnostics/gui_fuzz fuzz [s]     random/mutated input to every parser for s seconds (60)
//   ./diagnostics/gui_fuzz http [s]     hostile requests at a real Http_Server, then random ones (20)
//
// The sanitizer build is -O0 (the Makefile's GUI_FUZZ_FLAGS): at -O1 the
// compiler runs out of memory on this machine. Times and sizes are worth
// reading only from `make diagnostics/gui_fuzz_timing`, the same source at
// -O3 without sanitizers. Run from the repo root; ASAN_OPTIONS=detect_leaks=0.
//
// The fuzzer steps around the crashes `cases` shows (known_crasher()) so it
// can find others. Exit code 1 when a check fails: an invariant broken in
// `fuzz` (a parse that does not round-trip, a redirect target off this
// server), an `http` probe the server did not survive or answered wrongly, or
// a `cases` entry that names a fault. Memory errors end the run through the
// sanitizer, with its report.
#include "../gui/http_server.hpp"
#include "../gui/access.hpp"
#include "../gui/session.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

static int failures = 0;

static void fail(const std::string& what)
{
    std::printf("  FAIL %s\n", what.c_str());
    failures++;
}

static double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
}

static long max_rss_mb()
{
    rusage r{};
    getrusage(RUSAGE_SELF, &r);
    return r.ru_maxrss/1024;
}

// ------------------------------------------------------------------ inputs

// 258 legal moves with one king a side and nobody in check: everything the
// GUI's FEN check asked before #111's fix. A hill climb over queen placements
// found it; the GUI's and the engine's move buffers hold 256. uci_parse_fen
// now refuses it (more than 16 pieces a side).
static const char* FEN_258 = "QQQQQQpk/Q4Qpp/Q5Q1/Q5Q1/Q6Q/Q6Q/Q6Q/QQQQQQQK w - - 0 1";

// Black is in check with white to move: refused by /api/fen before #111's
// fix, but a PGN's FEN tag went through without that check. Both now go
// through uci_parse_fen's own in_check() refusal.
static const char* FEN_KING_EN_PRISE = "4k3/8/8/8/8/8/8/4R1K1 w - - 0 1";

static std::string uci_of(const BB& before, const BB* child)
{
    return get_UCI(&before, child);
}

// A game of `plies` moves as PGN movetext, chosen by `rng` among the legal
// ones, made through a scratch tree so every SAN is the one the reader expects.
static std::string long_line(int plies, std::mt19937& rng)
{
    std::unique_ptr<Move_Tree> t(new Move_Tree);
    BB start;
    uci_parse_fen(UCI_STARTPOS, start);
    t->start(start, 0, 1);
    std::string text;
    for(int p=0;p<plies && t->n_legal_moves()>0;p++)
    {
        int k = (int)(rng()%t->n_legal_moves());
        t->play(uci_of(t->position(), t->legal_moves()+k));
        text += t->current().san + " ";
    }
    return text + "*";
}

// Variations nested `depth` deep: after every move a variation opens on it
// and the game goes on inside it, so nothing closes until the end.
static std::string nested_variations(int depth, std::mt19937& rng)
{
    std::unique_ptr<Move_Tree> t(new Move_Tree);
    BB start;
    uci_parse_fen(UCI_STARTPOS, start);
    t->start(start, 0, 1);
    std::string text;
    int open = 0;
    for(int d=0;d<depth && t->n_legal_moves()>0;d++)
    {
        int n = t->n_legal_moves();
        int from = t->cursor_id();
        int a = (int)(rng()%n);
        t->play(uci_of(t->position(), t->legal_moves()+a));
        text += t->current().san + " ";
        if(n<2)
        continue;
        int b = (a+1+(int)(rng()%(n-1)))%n;
        t->go_to(from);
        t->play(uci_of(t->position(), t->legal_moves()+b));
        text += "( " + t->current().san + " ";
        open++;
    }
    return text + std::string(open, ')') + " *";
}

// ------------------------------------------------------------------ cases

struct Case
{
    const char* name;
    const char* what;
    std::function<void()> run;
};

static void time_load(const std::string& label, const std::string& pgn, bool keep_session = false,
                      std::function<void(Session&)> after = nullptr)
{
    std::unique_ptr<Session> s(new Session("fuzz"));
    long rss0 = max_rss_mb();
    auto t0 = std::chrono::steady_clock::now();
    std::string error;
    bool ok = s->load_pgn(pgn, error);
    double load_s = seconds_since(t0);
    std::printf("  %s: %zu bytes, load %s in %.2f s, %d nodes, max RSS +%ld MB\n", label.c_str(), pgn.size(),
                ok ? "ok" : ("refused (" + error.substr(0, 60) + ")").c_str(), load_s, ok ? s->game_tree().size() : 0,
                max_rss_mb()-rss0);
    if(!ok)
    return;
    t0 = std::chrono::steady_clock::now();
    std::string state = s->state_json();
    std::printf("  %s: state_json %zu bytes in %.2f s\n", label.c_str(), state.size(), seconds_since(t0));
    t0 = std::chrono::steady_clock::now();
    std::string out = s->pgn();
    std::printf("  %s: pgn() %zu bytes in %.2f s\n", label.c_str(), out.size(), seconds_since(t0));
    if(after)
    after(*s);
}

static std::vector<Case> cases()
{
    std::vector<Case> list;
    list.push_back({"fen258", "/api/fen with a 258-move position: accepted, and the tree's move list holds 256",
        [] {
            std::unique_ptr<Session> s(new Session("fuzz"));
            std::string error;
            bool ok = s->set_fen(FEN_258, error);
            std::printf("  set_fen: %s; the tree counts %d legal moves in a %d-slot array\n",
                        ok ? "accepted" : error.c_str(), s->game_tree().n_legal_moves(), MAX_LEGAL_MOVES);
            if(ok && s->game_tree().n_legal_moves()>MAX_LEGAL_MOVES)
            fail("a position with more legal moves than MAX_LEGAL_MOVES is accepted (the array was overrun)");
        }});
    list.push_back({"fen258-stack", "the same position straight into uci_parse_fen: must now be refused",
        [] {
            BB pos;
            bool ok = uci_parse_fen(FEN_258, pos);
            std::printf("  uci_parse_fen: %s\n", ok ? "accepted" : "refused");
            if(ok)
            fail("uci_parse_fen accepts a 258-move position");
        }});
    list.push_back({"pgn-fen258", "a PGN whose FEN tag is the 258-move position",
        [] {
            time_load("pgn-fen258", std::string("[FEN \"") + FEN_258 + "\"]\n\n1. Qa1b1 *");
        }});
    list.push_back({"pgn-king-capture", "a PGN whose FEN tag has the side not to move in check, then the king taken",
        [] {
            time_load("pgn-king-capture", std::string("[FEN \"") + FEN_KING_EN_PRISE + "\"]\n\n1. Rxe8 *", false,
                [](Session& s) {
                    std::printf("  the board after it: %s\n", s.game_tree().fen().c_str());
                    fail("a position without a king reached the tree");
                });
        }});
    list.push_back({"pgn-long", "a 1 MB PGN of one long line (the request cap)",
        [] {
            std::mt19937 rng(7);
            std::string pgn;
            for(int plies=20000;;plies*=2)
            {
                pgn = long_line(plies, rng);
                if(pgn.size()>=900000 || plies>400000)
                break;
            }
            time_load("pgn-long", pgn.substr(0, MAX_REQUEST_BYTES-200) + " *", false, [](Session& s) {
                auto t0 = std::chrono::steady_clock::now();
                std::string error;
                s.go_to(1, error);
                bool ok = s.delete_variation(error);
                std::printf("  delete from move 1 (recursive, one call per move): %s in %.2f s\n",
                            ok ? "ok" : error.c_str(), seconds_since(t0));
            });
        }});
    list.push_back({"pgn-deep", "variations nested 40 000 deep, ~440 kB (export recursion; GUI_FUZZ_DEPTH=n for n)",
        [] {
            std::mt19937 rng(11);
            const char* given = std::getenv("GUI_FUZZ_DEPTH");
            std::string pgn = nested_variations(given ? std::atoi(given) : 40000, rng);
            std::printf("  %zu bytes, %zu levels\n", pgn.size(), (size_t)std::count(pgn.begin(), pgn.end(), '('));
            std::fflush(stdout);
            time_load("pgn-deep", pgn);
        }});
    list.push_back({"pgn-invalid-end", "a 1 MB PGN whose last move is illegal: all the work, then refused",
        [] {
            std::mt19937 rng(7);
            std::string pgn = long_line(150000, rng);
            pgn = pgn.substr(0, std::min(pgn.size(), MAX_REQUEST_BYTES-200));
            pgn = pgn.substr(0, pgn.rfind(' ')) + " Kz9 *";
            time_load("pgn-invalid-end", pgn);
        }});
    list.push_back({"line-long", "/api/line with 1 MB of moves",
        [] {
            std::mt19937 rng(5);
            std::unique_ptr<Move_Tree> t(new Move_Tree);
            BB start;
            uci_parse_fen(UCI_STARTPOS, start);
            t->start(start, 0, 1);
            std::string moves;
            while(moves.size()<MAX_REQUEST_BYTES-200 && t->n_legal_moves()>0)
            {
                std::string uci = uci_of(t->position(), t->legal_moves()+rng()%t->n_legal_moves());
                t->play(uci);
                moves += uci + " ";
            }
            std::unique_ptr<Session> s(new Session("fuzz"));
            auto t0 = std::chrono::steady_clock::now();
            std::string error;
            bool ok = s->enter_line(moves, error);
            std::printf("  enter_line: %zu bytes, %s in %.2f s, %d nodes\n", moves.size(), ok ? "ok" : error.c_str(),
                        seconds_since(t0), s->game_tree().size());
        }});
    list.push_back({"json-deep", "a request body of nested brackets and long strings",
        [] {
            std::map<std::string, std::string> out;
            bool a = json::parse_flat_object(std::string(500000, '[') , out);
            bool b = json::parse_flat_object("{\"a\":\"" + std::string(900000, 'x') + "\"}", out);
            bool c = json::parse_flat_object("{\"a\":\"\\u", out);
            bool d = json::parse_flat_object("{\"a\":\"\\uzzzz\"}", out);
            std::printf("  brackets %s, 900 kB string %s, cut \\u %s, \\uzzzz %s (value byte %d)\n",
                        a ? "parsed" : "refused", b ? "parsed" : "refused", c ? "parsed" : "refused",
                        d ? "parsed" : "refused", d ? (int)(unsigned char)out["a"][0] : -1);
        }});
    list.push_back({"cookie", "login cookies: forged, huge expiry, long, empty",
        [] {
            gui_access::Login login;
            std::string error;
            login.setup("pw", "0123456789abcdef0123", error);
            long long now = 1700000000;
            std::string good = login.make_cookie(now);
            const std::string tries[] = {"", ".", "1.", ".x", "999999999999999999." + good.substr(good.find('.')+1),
                                         "9999999999999999999999.x", good + "0", std::string(1<<16, '9') + ".x",
                                         good.substr(0, good.size()-1) + (good.back()=='0' ? "1" : "0")};
            for(const std::string& c : tries)
            if(login.cookie_ok(c, now))
            fail("a forged cookie is accepted: " + c.substr(0, 40));
            if(!login.cookie_ok(good, now))
            fail("the real cookie is refused");
            std::printf("  forged cookies refused, the real one accepted\n");
        }});
    list.push_back({"login-limiter", "one IPv6 /64, a billion addresses: the group hits the per-IP limit, not just the total",
        [] {
            gui_access::Login_Limiter lim;
            lim.per_ip = 10;
            lim.total = 100;
            long long now = 1700000000000;
            bool whole_server = false;
            // 10 different addresses in 2001:db8:abcd:1234::/64, one failure each.
            for(int i=0;i<10;i++)
            {
                char ip[64];
                std::snprintf(ip, sizeof ip, "2001:db8:abcd:1234::%x", i+1);
                if(lim.wait_ms(ip, now, whole_server)!=0)
                fail(std::string("address ")+ip+" refused before its own first try");
                lim.failed(ip, now);
            }
            // An 11th, never seen before, in the same /64: refused as that group, not the total.
            long long wait = lim.wait_ms("2001:db8:abcd:1234::beef", now, whole_server);
            if(wait<=0 || whole_server)
            fail("an 11th address in the same /64 is not refused as its group");
            // A address one /64 over is untouched.
            if(lim.wait_ms("2001:db8:abcd:1235::1", now, whole_server)!=0)
            fail("a different /64 is refused by another group's failures");
            std::printf("  a full /64 group is refused together, a neighboring /64 is unaffected\n");
        }});
    return list;
}

static int run_cases(const std::string& only)
{
    for(const Case& c : cases())
    {
        if(!only.empty() && only!=c.name)
        continue;
        std::printf("%s — %s\n", c.name, c.what);
        std::fflush(stdout);
        c.run();
        std::fflush(stdout);
    }
    return failures ? 1 : 0;
}

// ------------------------------------------------------------------ fuzz

// Random text over `alphabet`, up to `max_len` long.
static std::string random_text(std::mt19937& rng, const std::string& alphabet, size_t max_len)
{
    size_t n = rng()%(max_len+1);
    std::string s(n, ' ');
    for(char& c : s)
    c = alphabet[rng()%alphabet.size()];
    return s;
}

// A seed with a few bytes flipped, inserted, deleted or duplicated.
static std::string mutate(std::mt19937& rng, std::string s, const std::string& alphabet)
{
    int edits = 1+(int)(rng()%8);
    for(int e=0;e<edits;e++)
    {
        size_t at = s.empty() ? 0 : rng()%s.size();
        switch(rng()%5)
        {
            case 0: if(!s.empty()) s[at] = alphabet[rng()%alphabet.size()]; break;
            case 1: s.insert(s.begin()+at, alphabet[rng()%alphabet.size()]); break;
            case 2: if(!s.empty()) s.erase(at, 1+rng()%std::min<size_t>(8, s.size()-at)); break;
            case 3: s.insert(at, s.substr(at, rng()%32)); break;
            case 4: s.insert(at, 1, (char)(rng()%256)); break;
        }
    }
    return s;
}

static const std::vector<std::string> PGN_SEEDS = {
    "[Event \"x\"]\n[White \"a\"]\n\n1. e4 e5 2. Nf3 (2. f4 exf4 {gambit} 3. Nf3) 2... Nc6 3. Bb5 a6 *",
    "1. d4 d5 2. c4 (2. Nf3 Nf6 (2... c5 3. dxc5) 3. Bf4) 2... e6 3. Nc3 Nf6 1-0",
    "[SetUp \"1\"]\n[FEN \"4k3/P7/8/8/8/8/7p/4K3 w - - 0 1\"]\n\n1. a8=Q+ Kd7 2. Qb7+ Ke6 (2... Kd6) 3. Kf2 h1=N+ *",
    "[FEN \"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1\"]\n\n1. O-O O-O-O 2. Rxf8 $1 ; comment\n Rxf8 1/2-1/2",
    "[FEN \"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3\"]\n\n3. exf6 e5 4. fxg7 Ke7 5. gxh8=Q *",
    "1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 a6 {Najdorf} (5...Nc6 6.Bg5) 6.Be3 e5 7.Nb3 Be6 0-1",
};

static const std::vector<std::string> FEN_SEEDS = {
    UCI_STARTPOS, "4k3/8/8/8/8/8/8/4K3 w - - 0 1", "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 12 40",
    "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", "8/P6k/8/8/8/8/6p1/K7 w - - 99 120",
    FEN_258, FEN_KING_EN_PRISE,
};

static const std::vector<std::string> JSON_SEEDS = {
    "{\"id\":\"main\",\"uci\":\"e2e4\"}", "{\"kind\":\"clock\",\"baseMs\":60000,\"incMs\":1000}",
    "{ \"pgn\" : \"1. e4 \\\"x\\\" \\u00e9\\n\" , \"on\":true }", "{}", "{\"a\":null,\"b\":-1.5e3}",
};

// The two crashes `cases` shows (fen258, pgn-king-capture), named so the
// fuzzer steps around them and goes on looking for others: "" for a FEN that
// is neither.
static std::string known_crasher(const std::string& fen)
{
    BB pos;
    if(!uci_parse_fen(fen, pos))
    return "";
    static std::unique_ptr<BB[]> big(new BB[4096]);
    if(std::get<0>(all_moves(&pos, big.get(), 4000))>MAX_LEGAL_MOVES)
    return "more than 256 legal moves";
    if(in_check(pos.Board, !pos.white_move))
    return "the side not to move in check";
    return "";
}

static int run_fuzz(double budget_s)
{
    std::mt19937 rng(20261011);
    const std::string any = [] { std::string s; for(int c=1;c<256;c++) s += (char)c; return s + std::string(1, '\0'); }();
    const std::string pgn_chars = "abcdefghKQRBNxO-+#=12345678. ()[]{};$*/\"\n\t0e";
    const std::string fen_chars = "pnbrqkPNBRQK12345678/ wb-KQkqabcdefgh0369";
    const std::string url_chars = "/abcAZ09%._-?&=+;:\\\"'<> \t\r\n\x01\x7f\xff";
    const std::string json_chars = "{}[]\":,\\u0123456789abcdefnrt tf-+.e\n";

    gui_access::Login login;
    std::string error;
    login.setup("correct horse", "0123456789abcdef0123456789abcdef", error);

    std::unique_ptr<Session> session(new Session("fuzz"));
    long long runs = 0, pgn_loaded = 0, fen_accepted = 0, json_parsed = 0;
    std::map<std::string, long long> tally;
    auto t0 = std::chrono::steady_clock::now();
    while(seconds_since(t0)<budget_s)
    {
        runs++;
        // URL and form decoding, cookies, the redirect target, the client address.
        std::string url = rng()%2 ? random_text(rng, url_chars, 64) : random_text(rng, any, 64);
        std::string decoded = Request::decode(url);
        Request::field(url, "next");
        Request::field(url, "");
        std::string next = gui_access::safe_next(decoded);
        if(next.empty() || next[0]!='/' || (next.size()>1 && (next[1]=='/' || next[1]=='\\'))
           || next.find_first_of("\r\n\0\\\"<>", 0, 7)!=std::string::npos)
        fail("safe_next lets through \"" + next + "\"");
        std::string encoded = gui_access::url_encode(next);
        if(Request::decode(encoded)!=next)
        fail("url_encode does not round-trip \"" + next + "\"");
        std::string escaped = gui_access::html_escape(url);
        if(escaped.find_first_of("<>\"'")!=std::string::npos)
        fail("html_escape leaves a markup character");
        gui_access::cookie_value(url, "gui_login");
        if(login.cookie_ok(url, 1700000000) || login.cookie_ok(gui_access::cookie_value(url, "gui_login"), 1700000000))
        fail("a random cookie is accepted");
        std::map<std::string, std::string> headers = {{"x-real-ip", url}, {"x-forwarded-for", decoded}, {"x-forwarded-proto", url}};
        gui_access::Client who = gui_access::client_of("172.17.0.1", headers, true);
        if(!gui_access::valid_ip(who.ip))
        fail("client_of returns a non-address \"" + who.ip + "\"");

        // JSON request bodies: what parses must write back and parse the same.
        std::string body = rng()%3 ? mutate(rng, JSON_SEEDS[rng()%JSON_SEEDS.size()], json_chars)
                                   : random_text(rng, rng()%2 ? json_chars : any, 80);
        std::map<std::string, std::string> parsed, again;
        if(json::parse_flat_object(body, parsed))
        {
            json_parsed++;
            json::Out o;
            o.obj();
            for(const auto& kv : parsed)
            o.key(kv.first).str(kv.second);
            o.end_obj();
            if(!json::parse_flat_object(o.s, again) || again!=parsed)
            fail("JSON does not round-trip: " + body.substr(0, 60));
        }

        // FEN: an accepted position is one the GUI's move buffers can hold.
        std::string fen = rng()%4 ? mutate(rng, FEN_SEEDS[rng()%FEN_SEEDS.size()], fen_chars)
                                  : random_text(rng, fen_chars, 90);
        BB pos;
        if(uci_parse_fen(fen, pos))
        {
            std::string known = known_crasher(fen);
            std::string ignored;
            if(!known.empty())
            tally["FEN skipped, " + known]++;
            else if(session->set_fen(fen, ignored))
            fen_accepted++;
        }

        // PGN: a game that loads writes out as PGN that loads to the same PGN.
        std::string pgn = rng()%4 ? mutate(rng, PGN_SEEDS[rng()%PGN_SEEDS.size()], pgn_chars)
                                  : random_text(rng, rng()%2 ? pgn_chars : any, 200);
        std::string pgn_error, known;
        for(size_t at=pgn.find("[FEN"); at!=std::string::npos && known.empty(); at=pgn.find("[FEN", at+1))
        {
            size_t end = pgn.find(']', at), q1 = pgn.find('"', at);
            size_t q2 = end==std::string::npos ? std::string::npos : pgn.rfind('"', end);
            if(q1<end && q2!=std::string::npos && q2>q1)
            known = known_crasher(pgn.substr(q1+1, q2-q1-1));
        }
        if(!known.empty())
        tally["PGN skipped, its FEN tag " + known]++;
        else if(session->load_pgn(pgn, pgn_error))
        {
            pgn_loaded++;
            const BB& root = session->game_tree().root_position();
            if(__builtin_popcountll(root.Board[5])!=1 || __builtin_popcountll(root.Board[11])!=1)
            tally["PGN start without exactly one king a side"]++;
            if(in_check(root.Board, !root.white_move))
            tally["PGN start with the side not to move in check"]++;
            std::string out = session->pgn();
            session->state_json();
            std::unique_ptr<Session> copy(new Session("copy"));
            if(!copy->load_pgn(out, pgn_error))
            fail("exported PGN does not load back (" + pgn_error + "): " + pgn.substr(0, 80));
            else if(copy->pgn()!=out)
            fail("exported PGN changes on a round trip: " + pgn.substr(0, 80));
        }
        if(failures>20)
        break;
    }
    std::printf("fuzz: %lld runs in %.0f s; JSON parsed %lld, FEN accepted %lld, PGN loaded %lld; %d failures\n",
                runs, seconds_since(t0), json_parsed, fen_accepted, pgn_loaded, failures);
    for(const auto& kv : tally)
    std::printf("  seen %lld times: %s\n", kv.second, kv.first.c_str());
    return failures ? 1 : 0;
}

// ------------------------------------------------------------------ http

static int connect_to(int port, int timeout_s = 15)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(connect(fd, (sockaddr*)&addr, sizeof addr)!=0)
    {
        close(fd);
        return -1;
    }
    timeval tv{timeout_s, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

static void send_all(int fd, const std::string& data)
{
    for(size_t at=0;at<data.size();)
    {
        ssize_t n = send(fd, data.data()+at, data.size()-at, MSG_NOSIGNAL);
        if(n<=0)
        return;
        at += n;
    }
}

static std::string read_all(int fd)
{
    std::string out;
    char buf[8192];
    for(ssize_t n; (n = recv(fd, buf, sizeof buf, 0))>0; )
    out.append(buf, n);
    return out;
}

// Sends `request` on a fresh connection; the status line of the answer, or "".
static std::string http_exchange(int port, const std::string& request, bool half_close = true, int timeout_s = 15)
{
    int fd = connect_to(port, timeout_s);
    if(fd<0)
    return "";
    send_all(fd, request);
    if(half_close)
    shutdown(fd, SHUT_WR);
    std::string answer = read_all(fd);
    close(fd);
    return answer.substr(0, answer.find("\r\n"));
}

static double thread_cpu_s(pthread_t thread)
{
    clockid_t id;
    timespec ts{};
    if(pthread_getcpuclockid(thread, &id)!=0 || clock_gettime(id, &ts)!=0)
    return -1;
    return ts.tv_sec + ts.tv_nsec/1e9;
}

static int run_http(double budget_s)
{
    Http_Server server;
    server.max_connections = 64;
    long long handled = 0;
    server.handler = [&](const Request& req) {
        handled++;
        // Every field the handlers read, so a bad one shows up here.
        std::string text = req.method + " " + req.path + " " + req.query + " " + std::to_string(req.body.size());
        for(const auto& h : req.headers)
        text += h.first.size() + h.second.size();
        return Response::text(text);
    };
    std::mt19937 rng(42);
    int first = 20000 + (int)(rng()%20000);
    if(!server.listen_on(first))
    {
        std::printf("http: no free port from %d\n", first);
        return 1;
    }
    int port = server.port();
    std::thread loop([&] { server.run(); });
    pthread_t loop_handle = loop.native_handle();
    auto alive = [&](const std::string& after) {
        std::string status = http_exchange(port, "GET /alive HTTP/1.1\r\nHost: x\r\n\r\n");
        if(status!="HTTP/1.1 200 OK")
        fail("server does not answer after " + after + " (got \"" + status + "\")");
    };

    struct Probe { const char* name; std::string request; const char* expect; };
    const std::string body_1mb(MAX_REQUEST_BYTES, 'a');
    std::vector<Probe> probes = {
        {"plain GET", "GET / HTTP/1.1\r\n\r\n", "200"},
        {"Content-Length past 2^64", "POST /x HTTP/1.1\r\nContent-Length: 99999999999999999999999\r\n\r\nabc", "200"},
        {"Content-Length -1", "POST /x HTTP/1.1\r\nContent-Length: -1\r\n\r\nabc", "200"},
        {"Content-Length wrapping size_t", "POST /x HTTP/1.1\r\nContent-Length: 18446744073709551600\r\n\r\nabc", "200"},
        {"Content-Length larger than sent", "POST /x HTTP/1.1\r\nContent-Length: 100\r\n\r\nabc", ""},
        {"two Content-Lengths", "POST /x HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 0\r\n\r\nabc", "200"},
        {"body over 1 MB", "POST /x HTTP/1.1\r\nContent-Length: " + std::to_string(body_1mb.size()) + "\r\n\r\n" + body_1mb, "413"},
        {"header over 1 MB, no end", "GET / HTTP/1.1\r\nX: " + body_1mb, "413"},
        {"no version", "GET /\r\n\r\n", "400"},
        {"no spaces", "GET\r\n\r\n", "400"},
        {"bare LF", "GET /a HTTP/1.1\nHost: x\n\n", "200"},
        {"NUL and %00 in path", std::string("GET /a\0b%00%zz%%%4 HTTP/1.1\r\n\r\n", 32), "200"},
        {"lone %", "GET /% HTTP/1.1\r\n\r\n", "200"},
        {"100 000 headers", "GET / HTTP/1.1\r\n" + [] { std::string h; for(int i=0;i<100000;i++) h += "a" + std::to_string(i) + ":b\r\n"; return h; }() + "\r\n", "200"},
        {"pipelined second request", "GET /1 HTTP/1.1\r\n\r\nGET /2 HTTP/1.1\r\n\r\n", "200"},
        {"binary garbage", std::string("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03", 11) + "\r\n\r\n", "400"},
    };
    for(const Probe& p : probes)
    {
        auto t0 = std::chrono::steady_clock::now();
        std::string status = http_exchange(port, p.request);
        std::printf("  %-34s -> %-32s %.2f s\n", p.name, status.empty() ? "(closed)" : status.c_str(), seconds_since(t0));
        if(*p.expect && status.find(p.expect)==std::string::npos)
        fail(std::string(p.name) + ": expected " + p.expect);
        alive(p.name);
    }

    // A header that never ends, 1 MB of it a few bytes per packet. The loop
    // searched it from the start on every read (~95% CPU) until #111 made it
    // search only the new bytes.
    {
        int fd = connect_to(port);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, 1 /* TCP_NODELAY */, &one, sizeof one);
        double cpu0 = thread_cpu_s(loop_handle);
        auto t0 = std::chrono::steady_clock::now();
        std::string chunk(200, '\r');
        size_t sent = 0;
        while(sent<MAX_REQUEST_BYTES && seconds_since(t0)<9)
        {
            send_all(fd, sent==0 ? "GET / HTTP/1.1\r\nX: " : chunk);
            sent += chunk.size();
            usleep(500);
        }
        double cpu = thread_cpu_s(loop_handle)-cpu0;
        std::printf("  dribbled header: %zu kB in %.1f s cost the server loop %.2f s CPU\n", sent/1000, seconds_since(t0), cpu);
        close(fd);
        alive("dribbled header");
    }

    // The connection cap: 64 idle connections, then one more.
    {
        std::vector<int> idle;
        for(int i=0;i<64;i++)
        idle.push_back(connect_to(port));
        usleep(200000);
        std::string status = http_exchange(port, "GET / HTTP/1.1\r\n\r\n");
        std::printf("  65th connection with 64 idle ones open -> %s\n", status.c_str());
        if(status.find("503")==std::string::npos)
        fail("the 65th connection is not refused");
        for(int fd : idle)
        close(fd);
        usleep(200000);
        alive("closing 64 idle connections");
    }

    // Random and mutated requests, each followed by nothing: the server must
    // still answer at the end, and the sanitizers must stay quiet.
    const std::string seeds[] = {"GET /api/state?id=main HTTP/1.1\r\nHost: a\r\nCookie: gui_login=1.2\r\n\r\n",
                                 "POST /api/move HTTP/1.1\r\nContent-Length: 15\r\n\r\n{\"uci\":\"e2e4\"}",
                                 "POST /login HTTP/1.1\r\nX-Real-IP: 1.2.3.4\r\nContent-Length: 11\r\n\r\npassword=x&"};
    const std::string http_chars = "GETPOS /?&=%:\r\n0123456789abcxyzContent-Length";
    auto t0 = std::chrono::steady_clock::now();
    long long sent = 0;
    while(seconds_since(t0)<budget_s && failures<5)
    {
        std::string req = rng()%3 ? mutate(rng, seeds[rng()%3], http_chars)
                                  : random_text(rng, http_chars, 300);
        http_exchange(port, req, true, 1);   // a body still owed is the server's 10 s timeout, not news
        sent++;
        if(sent%200==0)
        alive("random requests");
    }
    alive("random requests");
    std::printf("http: %lld random requests, %lld handled; %d failures\n", sent, handled, failures);
    server.stopping = 1;
    server.wake();
    loop.join();
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    std::signal(SIGPIPE, SIG_IGN);

    std::string mode = argc>1 ? argv[1] : "cases";
    if(mode=="cases")
    return run_cases("");
    if(mode=="case" && argc>2)
    return run_cases(argv[2]);
    if(mode=="fuzz")
    return run_fuzz(argc>2 ? std::atof(argv[2]) : 60);
    if(mode=="http")
    return run_http(argc>2 ? std::atof(argv[2]) : 20);
    std::fprintf(stderr, "usage: gui_fuzz cases | case <name> | fuzz [seconds] | http [seconds]\n");
    return 2;
}
