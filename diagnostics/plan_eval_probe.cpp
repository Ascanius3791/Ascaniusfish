// OWNERSHIP=Claude
// Plan eval (#43, lib/plan_eval.hpp). Prints, for a few positions, each piece's
// best target square with N, db and its term, then checks colour symmetry: a
// colour-mirrored position (ranks flipped, colours and side to move swapped)
// scores exactly the negated plan_eval() and basic_eval(), over the opening
// suite and positions played on from it. Also times plan_eval() there.
// Exit code 1 on a symmetry failure.
//
// Build from the repo root:
//   g++ -O3 -mpopcnt -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/plan_eval_probe diagnostics/plan_eval_probe.cpp
//   ./diagnostics/plan_eval_probe [fen ...]      (no FEN: the built-in positions)
#include "../lib/uci.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void expect(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if(!ok)
    failures++;
}

static BB from_fen(const std::string& fen)
{
    BB pos;
    if(!uci_parse_fen(fen, pos))
    {
        std::printf("bad FEN: %s\n", fen.c_str());
        std::exit(2);
    }
    return pos;
}

// Ranks flipped, colours swapped, side to move swapped (as in king_safety_test).
static std::string mirror_fen(const std::string& fen)
{
    std::istringstream in(fen);
    std::string placement, side, castling, ep;
    in >> placement >> side >> castling >> ep;
    std::vector<std::string> ranks;
    std::stringstream ps(placement);
    std::string rank;
    while(std::getline(ps, rank, '/'))
    ranks.push_back(rank);
    std::string out;
    for(int i = (int)ranks.size() - 1; i >= 0; i--)
    {
        for(char& ch : ranks[i])
        ch = std::isupper((unsigned char)ch) ? std::tolower(ch) : std::toupper(ch);
        out += ranks[i];
        if(i)
        out += '/';
    }
    std::string c;
    if(castling == "-")
    c = "-";
    else
    {
        for(char ch : std::string("KQkq"))
        {
            char from = std::isupper((unsigned char)ch) ? std::tolower(ch) : std::toupper(ch);
            if(castling.find(from) != std::string::npos)
            c += ch;
        }
    }
    if(ep != "-")
    ep[1] = ep[1] == '3' ? '6' : '3';
    return out + " " + (side == "w" ? "b" : "w") + " " + c + " " + ep + " 0 1";
}

static std::string sq_name(int sq)
{
    if(sq < 0)
    return "--";
    return std::string(1, char('a' + sq % 8)) + char('1' + sq / 8);
}

static void show(const std::string& fen, const char* what)
{
    BB pos = from_fen(fen);
    Plan_Target t[32];
    int n = 0;
    int total = plan_eval_detail(&pos, WEIGHTS_OG, t, &n);
    std::printf("\n%s\n  %s\n  plan_eval %+d (white's view), basic_eval %+d\n", what, fen.c_str(), total, basic_eval(&pos, WEIGHTS_OG));
    std::printf("  piece  from  to    N    db  term\n");
    for(int i = 0; i < n; i++)
    {
        const char c = "PRNBQK"[t[i].piece % 6];
        std::printf("  %c      %s    %s  %3d %5d %5d\n", t[i].piece < 6 ? c : (char)std::tolower(c),
                    sq_name(t[i].from).c_str(), sq_name(t[i].to).c_str(), t[i].n, t[i].db, t[i].term);
    }
}

static void symmetry_and_timing()
{
    std::vector<std::string> fens;
    std::ifstream epd("tools/openings.epd");
    std::string line;
    while(std::getline(epd, line))
    {
        if(line.empty() || line[0] == '#')
        continue;
        std::istringstream in(line);
        std::string a, b, c, d;
        in >> a >> b >> c >> d;
        fens.push_back(a + " " + b + " " + c + " " + d + " 0 1");
    }
    if(fens.empty())
    {
        expect(false, "tools/openings.epd not found (run from the repo root)");
        return;
    }
    BB* wfh = new BB[256];
    std::vector<BB> positions;
    unsigned rng = 12345;
    for(const std::string& fen : fens)
    {
        BB pos = from_fen(fen);
        for(int ply = 0; ply < 60; ply++)
        {
            positions.push_back(pos);
            int n = std::get<0>(all_moves(&pos, wfh));
            if(n == 0)
            break;
            rng = rng * 1103515245u + 12345u;
            pos = wfh[(rng >> 16) % n];
        }
    }
    delete[] wfh;

    int plan_bad = 0, eval_bad = 0, ref_bad = 0;
    long long sum_abs = 0;
    for(const BB& pos : positions)
    {
        BB copy = pos;
        BB m = from_fen(mirror_fen(copy.get_FEN()));
        Plan_Target fast[32], ref[32];
        int nf = 0, nr = 0;
        int p = plan_eval_detail(&pos, WEIGHTS_OG, fast, &nf);
        bool same = plan_eval_detail(&pos, WEIGHTS_OG, ref, &nr, true) == p && nf == nr;
        for(int i = 0; same && i < nf; i++)
        same = fast[i].to == ref[i].to && fast[i].n == ref[i].n && fast[i].db == ref[i].db && fast[i].term == ref[i].term;
        if(!same)
        {
            if(ref_bad < 3)
            std::printf("      fast != reference: %s\n", copy.get_FEN().c_str());
            ref_bad++;
        }
        if(plan_eval_detail(&m, WEIGHTS_OG, nullptr, nullptr) != -p)
        plan_bad++;
        if(basic_eval(&m, WEIGHTS_OG) + basic_eval(&pos, WEIGHTS_OG) != 0)
        eval_bad++;
        sum_abs += p < 0 ? -p : p;
    }
    std::string n = std::to_string(positions.size());
    std::printf("\n");
    expect(ref_bad == 0, "fast db equals the reference (partial evals recomputed): " + std::to_string(ref_bad) + "/" + std::to_string(positions.size()) + " differ");
    expect(plan_bad == 0, "plan_eval mirror-symmetric: " + std::to_string(plan_bad) + "/" + n + " broken");
    expect(eval_bad == 0, "basic_eval mirror-symmetric: " + std::to_string(eval_bad) + "/" + n + " broken");
    std::printf("      mean |plan_eval| %.1f cp\n", sum_abs / (double)positions.size());

    volatile int sink = 0;
    auto tr = std::chrono::steady_clock::now();
    for(const BB& pos : positions)
    sink += plan_eval_detail(&pos, WEIGHTS_OG, nullptr, nullptr, true);
    auto t0 = std::chrono::steady_clock::now();
    std::printf("      reference %.2f us/call\n", std::chrono::duration<double, std::micro>(t0 - tr).count() / positions.size());
    for(int rep = 0; rep < 3; rep++)
    for(const BB& pos : positions)
    sink += plan_eval_detail(&pos, WEIGHTS_OG, nullptr, nullptr);
    auto t1 = std::chrono::steady_clock::now();
    for(int rep = 0; rep < 3; rep++)
    for(const BB& pos : positions)
    sink += basic_eval(&pos, WEIGHTS_OG);
    auto t2 = std::chrono::steady_clock::now();
    double calls = 3.0 * positions.size();
    std::printf("      plan term %.2f us/call, basic_eval (with it, if on) %.2f us/call\n",
                std::chrono::duration<double, std::micro>(t1 - t0).count() / calls,
                std::chrono::duration<double, std::micro>(t2 - t1).count() / calls);
}

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    if(argc > 1)
    {
        for(int i = 1; i < argc; i++)
        show(argv[i], "given");
        return 0;
    }
    show("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "start position");
    show("r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 9", "Dragon, Yugoslav: d5 for the c3 knight?");
    show("r2q1rk1/1b2bppp/p2p1n2/1p2p3/4P3/1NN1B3/PPP1BPPP/R2Q1RK1 w - - 0 12", "Sicilian with a d5 hole");
    show("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", "rook on the back rank, open board");
    show("8/pp3k2/8/8/8/8/PP3K2/8 w - - 0 1", "pawn endgame: kings to the centre");

    symmetry_and_timing();
    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "all passed", failures);
    return failures ? 1 : 0;
}
