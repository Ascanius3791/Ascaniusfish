// OWNERSHIP=Claude
// King safety (#42, lib/king_safety.hpp). Checks:
//  1. attack: one piece at the king is no danger, a second one joining is, and so is
//     a pawn joining (#102) unless the ring square it hits two own pawns defend;
//  2. shelter: intact cover < an advanced pawn < a missing pawn < an open file,
//     and no cover penalty once the enemy has no pieces;
//  3. colour symmetry: a colour-mirrored position (ranks flipped, colours and
//     side to move swapped) scores exactly the negated king_safety_eval() and
//     basic_eval(), over the opening suite and positions played on from it;
//  4. the Italian from the issue: O-O scores above Ke2, Kd2, Kf1 and Rh1-f1.
// Exit code 1 on any failure. With a weight set as argv[1] (lib/weight_set.hpp)
// every check runs with it instead of WEIGHTS_OG: the symmetry check must hold for
// any set (#87), the others test set 1's shape.
//
// Build from the repo root:
//   g++ -O3 -mpopcnt -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/king_safety_test diagnostics/king_safety_test.cpp
#include "../lib/uci.hpp"
#include "../lib/king_safety.hpp"
#include "../lib/weight_set.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;
static WEIGHTS TEST_W = WEIGHTS_OG;

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

// Ranks flipped, colours swapped, side to move swapped.
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

static int basic(const BB& pos) { return basic_eval(&pos, TEST_W); }

static King_Safety_Detail detail(const std::string& fen, bool white)
{
    BB pos = from_fen(fen);
    return king_safety_detail(&pos, white, TEST_W);
}

static void attack_cases()
{
    struct Case { const char* fen; const char* what; };
    const Case harmless[] = {
        {"6k1/5ppp/8/8/7q/8/5PPP/6K1 w - - 0 1", "lone queen at the castled king"},
        {"6k1/5ppp/8/8/6n1/8/5PPP/6K1 w - - 0 1", "lone knight at the castled king"},
        {"6k1/5ppp/8/8/8/8/5PPP/4r1K1 w - - 0 1", "lone rook on the back rank"},
        {"6k1/5ppp/8/8/6np/8/5PPP/6K1 w - - 0 1", "knight + pawn h4 hitting only g3, which f2 and h2 defend"},
    };
    for(const Case& c : harmless)
    {
        King_Safety_Detail d = detail(c.fen, true);
        expect(d.attackers <= 1 && d.attack_penalty == 0,
               std::string(c.what) + ": attackers " + std::to_string(d.attackers)
               + ", penalty " + std::to_string(d.attack_penalty) + " (want 0)");
    }
    King_Safety_Detail qn = detail("6k1/5ppp/8/8/6nq/8/5PPP/6K1 w - - 0 1", true);
    expect(qn.attackers == 2 && qn.attack_penalty >= 50,
           "queen + knight at the castled king: attackers " + std::to_string(qn.attackers)
           + ", penalty " + std::to_string(qn.attack_penalty) + " (want >= 50)");
    // The queen on d8 does not reach the ring; it only keeps ks_no_queen out of it.
    King_Safety_Detail n = detail("3q2k1/5ppp/8/8/6n1/8/5PPP/6K1 w - - 0 1", true);
    King_Safety_Detail np = detail("3q2k1/5ppp/8/8/4p1n1/8/5PPP/6K1 w - - 0 1", true);
    expect(n.attackers == 1 && n.attack_penalty == 0 && np.attackers == 2 && np.attack_penalty > 0,
           "a pawn on e4 (hitting f3) joining the knight opens the gate: attackers " + std::to_string(n.attackers)
           + " -> " + std::to_string(np.attackers) + ", penalty " + std::to_string(n.attack_penalty)
           + " -> " + std::to_string(np.attack_penalty) + " (want 1 -> 2, 0 -> > 0)");
    King_Safety_Detail qnb = detail("6k1/5ppp/8/2b5/6nq/8/5PPP/6K1 w - - 0 1", true);
    expect(qnb.attackers == 3 && qnb.attack_penalty > qn.attack_penalty,
           "a bishop joining makes it worse: penalty " + std::to_string(qnb.attack_penalty));
}

static void shelter_cases()
{
    // Black keeps all its pieces, so the cover counts in full; only white's king is looked at.
    const std::string black = "rnbqkbnr/pppppppp/8/8/8/";
    struct Case { const char* white; const char* what; };
    const Case cases[] = {
        {"8/5PPP/6K1 w kq - 0 1", "f2 g2 h2"},
        {"7P/5PP1/6K1 w kq - 0 1", "f2 g2 h3"},
        {"8/5PP1/6K1 w kq - 0 1", "f2 g2, no h-pawn"},
    };
    int last = -1;
    for(const Case& c : cases)
    {
        int s = detail(black + c.white, true).shelter_penalty;
        expect(s > last, std::string("shelter ") + c.what + ": " + std::to_string(s)
               + " (want > " + std::to_string(last) + ")");
        last = s;
    }
    int open = detail("rnbqkbnr/pppppp1p/8/8/8/8/5P1P/6K1 w kq - 0 1", true).shelter_penalty;
    int half = detail("rnbqkbnr/pppppppp/8/8/8/8/5P1P/6K1 w kq - 0 1", true).shelter_penalty;
    expect(open > half, "open g-file " + std::to_string(open) + " > half-open " + std::to_string(half));
    int bare = detail("4k3/pppppppp/8/8/8/8/8/6K1 w - - 0 1", true).shelter_penalty;
    expect(bare == 0, "no enemy pieces, no cover: " + std::to_string(bare) + " (want 0)");
}

static void symmetry_cases()
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
    // Play on from every opening with a fixed pseudo-random walk, to reach middlegames
    // with open kings and attacks.
    BB* wfh = new BB[256];
    std::vector<BB> positions;
    unsigned rng = 12345;
    for(const std::string& fen : fens)
    {
        BB pos = from_fen(fen);
        for(int ply = 0; ply < 40; ply++)
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

    int ks_bad = 0, eval_bad = 0, attacked = 0;
    for(const BB& pos : positions)
    {
        BB copy = pos;
        BB m = from_fen(mirror_fen(copy.get_FEN()));
        if(king_safety_eval(&m, TEST_W) != -king_safety_eval(&pos, TEST_W))
        ks_bad++;
        if(basic(m) + basic(pos) != 0)
        eval_bad++;
        if(king_safety_detail(&pos, true, TEST_W).attack_penalty || king_safety_detail(&pos, false, TEST_W).attack_penalty)
        attacked++;
    }
    std::string n = std::to_string(positions.size());
    expect(ks_bad == 0, "king_safety_eval mirror-symmetric: " + std::to_string(ks_bad) + "/" + n + " broken ("
           + std::to_string(attacked) + " with an attack penalty)");
    expect(eval_bad == 0, "basic_eval mirror-symmetric: " + std::to_string(eval_bad) + "/" + n + " broken");
}

static void italian_case()
{
    BB root = from_fen("r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R w KQkq - 1 5");
    const char* moves[] = {"e1g1", "e1e2", "e1d2", "e1f1", "h1f1"};
    int score[5];
    for(int i = 0; i < 5; i++)
    {
        BB after;
        if(!uci_apply_move(root, moves[i], after))
        {
            expect(false, std::string("Italian: ") + moves[i] + " is not legal");
            return;
        }
        score[i] = basic(after);
        std::printf("      Italian %s: %d\n", moves[i], score[i]);
    }
    for(int i = 1; i < 5; i++)
    expect(score[0] > score[i], std::string("Italian: O-O (") + std::to_string(score[0]) + ") above "
           + moves[i] + " (" + std::to_string(score[i]) + ")");
}

int main(int argc, char** argv)
{
    if(argc>1)
    {
        Weight_Set_Info info;
        std::string error;
        if(!load_weight_set(argv[1], TEST_W, info, error))
        {
            std::printf("%s\n", error.c_str());
            return 2;
        }
        std::printf("weight set %d (%s)\n", TEST_W.version, argv[1]);
    }
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    attack_cases();
    shelter_cases();
    symmetry_cases();
    italian_case();

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "all passed", failures);
    return failures ? 1 : 0;
}
