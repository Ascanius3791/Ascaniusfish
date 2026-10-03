// OWNERSHIP=Claude
// The mate search (issue #78) on its own, on the mate suite: for every
// position, the quickest mate full width finds within a node budget, and the
// first one checks-only finds. A mate shorter than the suite's dm, or one that
// does not mate when its line is played out, is a bug (exit 1).
//
//   g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/mate_search_test diagnostics/mate_search_test.cpp
//   ./diagnostics/mate_search_test [suite=tools/mate_suite.epd] [nodes=20000000] [n=N] [mode=both|full|checks]
#include "../lib/uci.hpp"
#include "../lib/mate_search.hpp"
#include <chrono>
#include <fstream>
#include <map>

static std::string field(const std::string& line, const std::string& name)
{
    size_t at = line.find(name + " ");
    if(at == std::string::npos) return "";
    size_t end = line.find(';', at);
    return line.substr(at + name.size() + 1, end - at - name.size() - 1);
}

// Plays `line` from pos: true if it ends in mate after exactly `plies` moves.
static bool line_mates(const BB& pos, const std::vector<Move>& line, int plies, BB* wfh)
{
    if((int)line.size() != plies) return false;
    BB cur = pos;
    for(const Move& m : line)
    {
        Move_List moves;
        const int n = generate_legal_moves<GEN_ALL>(&cur, moves);
        bool legal = false;
        for(int i=0;i<n;i++) legal = legal || moves[i] == m;
        if(!legal) return false;
        make_move(&cur, m, wfh);
        cur = *wfh;
    }
    return cur.get_in_check() && !cur.get_has_legal_move();
}

// gives_check() against making the move, at pos and at each of its children
// (both sides as the attacker): the number of disagreements.
static int gives_check_errors(const BB& pos, BB* wfh, int depth)
{
    int errors = 0;
    Move_List moves;
    const int n = generate_legal_moves<GEN_ALL>(&pos, moves);
    const Node_Info info = node_info(&pos, pos.white_move);
    for(int i=0;i<n;i++)
    {
        const Move& m = moves[i];
        const int type = m.promotion_piece_type >= 0 ? m.promotion_piece_type : piece_on(pos.Board, m.from, pos.white_move);
        const int told = gives_check(&pos, m, info, type);
        make_move(&pos, m, wfh);
        if(told >= 0 && told != (int)wfh->get_in_check())
        {
            errors++;
            std::printf("gives_check wrong: %s move %d-%d says %d\n", get_FEN(pos).c_str(), m.from, m.to, told);
        }
        if(depth > 0)
        {
            BB child = *wfh;
            errors += gives_check_errors(child, wfh+1, depth-1);
        }
    }
    return errors;
}

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    std::map<std::string, std::string> opt;
    for(int i=1;i<argc;i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq != std::string::npos) opt[a.substr(0, eq)] = a.substr(eq+1);
    }
    auto get = [&](const std::string& k, const std::string& d) { return opt.count(k) ? opt[k] : d; };
    const long long budget = std::atoll(get("nodes", "20000000").c_str());
    const int only = std::atoi(get("n", "0").c_str());
    const std::string mode = get("mode", "both");
    std::ifstream f(get("suite", "tools/mate_suite.epd"));
    static BB wfh[4096];
    std::string line;
    int bugs = 0;
    std::map<int, std::array<long long, 8>> sum;  // n -> positions, full found, full exact, full nodes, checks found, checks exact, checks nodes, full ms
    while(std::getline(f, line))
    {
        if(line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string a, b, c, d;
        in >> a >> b >> c >> d;
        const int dm = std::atoi(field(line, "dm").c_str());
        if(only && dm != only) continue;
        BB pos;
        uci_parse_fen(a + " " + b + " " + c + " " + d + " 0 1", pos);
        const std::string id = field(line, "id");
        if(get("gives_check", "off") == "on") { bugs += gives_check_errors(pos, wfh, 2); continue; }
        auto& s = sum[dm];
        s[0]++;
        std::string report = "dm " + std::to_string(dm) + " " + id;
        for(int pass = 0; pass < 2; pass++)
        {
            const Mate_Mode mm = pass == 0 ? Mate_Mode::FULL_WIDTH : Mate_Mode::CHECKS_ONLY;
            if((pass == 0 && mode == "checks") || (pass == 1 && mode == "full")) continue;
            mate_tt_clear();
            auto t0 = std::chrono::steady_clock::now();
            Mate_Result r = find_mate(pos, wfh, pos.white_move, 2*dm - 1 + 4, mm, budget);
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            const int moves = (r.plies + 1)/2;
            const char* status = r.status == Mate_Result::FOUND ? "found" : r.status == Mate_Result::NONE ? "none" : "budget";
            char buf[200];
            std::snprintf(buf, sizeof buf, "  %s %s %d (%lld nodes, %lld ms)", pass == 0 ? "full" : "checks", status,
                          r.status == Mate_Result::FOUND ? moves : r.plies, r.nodes, ms);
            report += buf;
            if(r.status == Mate_Result::FOUND)
            {
                s[pass == 0 ? 1 : 4]++;
                if(moves == dm) s[pass == 0 ? 2 : 5]++;
                if(moves < dm) { report += " SHORTER"; bugs++; }
                if(pass == 0 && moves > dm) { report += " LONGER-THAN-QUICKEST"; bugs++; }
                if(!line_mates(pos, r.line, r.plies, wfh)) report += " (line " + std::to_string(r.line.size()) + " plies, does not mate)";
            }
            if(pass == 0 && r.status == Mate_Result::NONE) { report += " MISSED"; bugs++; }
            s[pass == 0 ? 3 : 6] += r.nodes;
            if(pass == 0) s[7] += ms;
        }
        std::printf("%s\n", report.c_str());
        std::fflush(stdout);
    }
    std::printf("\n%-8s %5s %12s %14s %12s %14s\n", "mate in", "pos", "full exact", "full Mnodes", "checks exact", "checks Mnodes");
    for(auto& [n, s] : sum)
    std::printf("%-8d %5lld %8lld/%-3lld %14.2f %8lld/%-3lld %14.2f\n", n, s[0], s[2], s[0], s[3]/1e6, s[5], s[0], s[6]/1e6);
    return bugs ? 1 : 0;
}
