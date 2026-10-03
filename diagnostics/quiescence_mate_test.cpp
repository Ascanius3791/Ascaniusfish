// OWNERSHIP=Claude
// Are the mates minimax_tactical() returns real (issue #78)? From every mate
// suite position, every check the side to move can give leads to a position
// in check; minimax_tactical() is run there with forced_moves_left = 0..2, so
// its out-of-budget paths (a quiet evasion that checks back is skipped) are
// reached at once. Every mate score it returns is checked with the full-width
// mate search: one that does not hold is a false mate (exit 1).
//
//   g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/quiescence_mate_test diagnostics/quiescence_mate_test.cpp
//   ./diagnostics/quiescence_mate_test [suite=tools/mate_suite.epd] [nodes=2000000]
#include "../lib/uci.hpp"
#include "../lib/mate_search.hpp"
#include <fstream>
#include <map>

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
    const long long budget = std::atoll(get("nodes", "2000000").c_str());
    std::ifstream f(get("suite", "tools/mate_suite.epd"));
    static BB wfh[4096];
    std::string line;
    long long nodes = 0, mates = 0, held = 0, false_mates = 0, unknown = 0;
    while(std::getline(f, line))
    {
        if(line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string a, b, c, d;
        in >> a >> b >> c >> d;
        BB pos;
        uci_parse_fen(a + " " + b + " " + c + " " + d + " 0 1", pos);
        // The positions in check after a check from pos, and (deep=on) after a
        // check, any reply and a check again.
        std::vector<BB> in_check;
        Move_List moves;
        const int n = generate_legal_moves<GEN_ALL>(&pos, moves);
        for(int i=0;i<n;i++)
        {
            make_move(&pos, moves[i], wfh);
            if(!wfh->get_in_check())
            continue;
            const BB checked = *wfh;
            in_check.push_back(checked);
            if(get("deep", "off") != "on")
            continue;
            Move_List replies;
            const int nr = generate_legal_moves<GEN_ALL>(&checked, replies);
            for(int j=0;j<nr;j++)
            {
                make_move(&checked, replies[j], wfh);
                const BB replied = *wfh;
                Move_List again;
                const int na = generate_legal_moves<GEN_ALL>(&replied, again);
                for(int k=0;k<na;k++)
                {
                    make_move(&replied, again[k], wfh);
                    if(wfh->get_in_check())
                    in_check.push_back(*wfh);
                }
            }
        }
        for(const BB& child : in_check)
        {
            for(int forced = 0; forced <= 2; forced++)
            {
                nodes++;
                const PV_Line q = minimax_tactical(&child, wfh+1, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr, forced);
                const bool white_mates = q.eval >= INT_MAX - max_mating_seq;
                if(!white_mates && q.eval > INT_MIN + max_mating_seq)
                continue;
                mates++;
                const int plies = white_mates ? INT_MAX - q.eval : q.eval - INT_MIN;
                mate_tt_clear();
                Mate_Result r = find_mate(child, wfh+1, white_mates, plies, Mate_Mode::FULL_WIDTH, budget);
                if(r.status == Mate_Result::FOUND) held++;
                else if(r.status == Mate_Result::UNKNOWN) unknown++;
                else
                {
                    false_mates++;
                    std::printf("FALSE MATE  %s  forced_moves_left %d  claims %d plies\n", get_FEN(child).c_str(), forced, plies);
                }
            }
        }
    }
    std::printf("%lld quiescence searches in check: %lld mate scores, %lld hold, %lld false, %lld unknown (budget)\n"
                "mate_confirmed(): %lld calls, %lld held\n", nodes, mates, held, false_mates, unknown, mate_checks, mate_checks_confirmed);
    return false_mates ? 1 : 0;
}
