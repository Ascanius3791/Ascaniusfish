// OWNERSHIP=Claude
// Builds the match opening suite (tools/openings.epd). One-off: the result
// is committed, this is kept so it can be rebuilt or extended.
//
//   ./tools/make_openings <out.epd> <count> <a.tsv> [b.tsv ...]
//
// Input: the named opening lines of github.com/lichess-org/chess-openings
// (a.tsv..e.tsv: eco, name, pgn). Lines of 8-16 plies are shuffled with a
// fixed seed; each variation (name up to the first ',') is used once and each
// family (name up to ':') at most FAMILY_CAP times. Every candidate is looked
// up in the Lichess cloud eval (lichess.org/api/cloud-eval, needs curl) and
// kept if |cp| <= MAX_CP at depth >= MIN_DEPTH.
#include "game_rules.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <thread>

constexpr int MIN_PLIES = 8, MAX_PLIES = 16;
constexpr int MAX_CP = 30, MIN_DEPTH = 20, FAMILY_CAP = 3;

struct Line { std::string eco, name, pgn; };

static std::string run_capture(const std::string& cmd)
{
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if(!p) return out;
    char buf[4096];
    while(fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    return out;
}

// Replays a SAN line from the start position; false on an unknown move.
static bool replay(const std::string& pgn, Game& game)
{
    BB start;
    uci_parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", start);
    game.start(start, 0, 1);
    std::istringstream in(pgn);
    std::string tok;
    while(in >> tok)
    {
        if(tok.back()=='.') continue;  // move number
        while(!tok.empty() && std::string("+#!?").find(tok.back())!=std::string::npos) tok.pop_back();
        const BB& pos = game.positions.back();
        std::string found;
        for(int k=0;k<game.n_children && found.empty();k++)
        {
            std::string s = san(pos, game.children, game.n_children, k);
            while(s.back()=='+' || s.back()=='#') s.pop_back();
            if(s==tok) found = get_UCI(&pos, game.children+k);
        }
        if(found.empty() || !game.play(found))
        return false;
    }
    return true;
}

// Cloud eval of `fen` (white's view). 0 = found, 1 = not in the cloud or a mate, -1 = error.
static int cloud_eval(const std::string& fen, int& cp, int& depth)
{
    std::string url_fen = fen;
    for(size_t i; (i = url_fen.find(' '))!=std::string::npos; ) url_fen.replace(i, 1, "%20");
    for(int attempt=0; attempt<5; attempt++)
    {
        std::string out = run_capture("curl -s -w '\\n%{http_code}' 'https://lichess.org/api/cloud-eval?fen=" + url_fen + "'");
        std::string code = out.substr(out.rfind('\n')+1);
        if(code=="404") return 1;
        if(code=="429") { std::fprintf(stderr, "rate limited, waiting 60 s\n"); std::this_thread::sleep_for(std::chrono::seconds(60)); continue; }
        if(code!="200") { std::this_thread::sleep_for(std::chrono::seconds(5)); continue; }
        size_t d = out.find("\"depth\":"), pv = out.find("\"pvs\":[{");
        if(d==std::string::npos || pv==std::string::npos) return -1;
        std::string first = out.substr(pv, out.find('}', pv)-pv);
        size_t c = first.find("\"cp\":");
        if(c==std::string::npos) return 1;  // mate score
        depth = std::atoi(out.c_str()+d+8);
        cp = std::atoi(first.c_str()+c+5);
        return 0;
    }
    return -1;
}

int main(int argc, char** argv)
{
    if(argc<4)
    {
        std::fprintf(stderr, "usage: %s <out.epd> <count> <a.tsv> [b.tsv ...]\n", argv[0]);
        return 2;
    }
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    int count = std::atoi(argv[2]);
    std::vector<Line> lines;
    for(int a=3;a<argc;a++)
    {
        std::ifstream in(argv[a]);
        std::string row;
        std::getline(in, row);  // header
        while(std::getline(in, row))
        {
            size_t t1 = row.find('\t'), t2 = row.find('\t', t1+1);
            if(t2==std::string::npos) continue;
            lines.push_back({row.substr(0, t1), row.substr(t1+1, t2-t1-1), row.substr(t2+1)});
        }
    }

    std::vector<std::pair<Line, Game*>> candidates;
    int unparsed = 0;
    for(const Line& l : lines)
    {
        Game* g = new Game;
        if(!replay(l.pgn, *g)) { unparsed++; delete g; continue; }
        int plies = g->uci_moves.size();
        std::string why;
        if(plies<MIN_PLIES || plies>MAX_PLIES || g->outcome(why)!=Outcome::ONGOING) { delete g; continue; }
        candidates.push_back({l, g});
    }
    std::printf("%zu lines, %d not parsed, %zu with %d-%d plies\n", lines.size(), unparsed, candidates.size(), MIN_PLIES, MAX_PLIES);
    std::mt19937 rng(20260925);
    std::shuffle(candidates.begin(), candidates.end(), rng);

    std::set<std::string> variations, placements;
    std::map<std::string, int> families;
    std::vector<std::pair<std::string, std::string>> out;  // (sort key, epd line)
    for(auto& [l, g] : candidates)
    {
        if((int)out.size()>=count) break;
        std::string variation = l.name.substr(0, l.name.find(','));
        std::string family = l.name.substr(0, l.name.find(':'));
        std::string fen = g->fen();
        std::string epd = fen.substr(0, fen.rfind(' ', fen.rfind(' ')-1));
        if(variations.count(variation) || families[family]>=FAMILY_CAP || placements.count(epd))
        continue;
        int cp = 0, depth = 0;
        int r = cloud_eval(fen, cp, depth);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        if(r!=0 || depth<MIN_DEPTH || std::abs(cp)>MAX_CP)
        {
            std::printf("  skip %-60.60s %s\n", l.name.c_str(), r ? "no cloud eval" : ("cp " + std::to_string(cp)).c_str());
            continue;
        }
        variations.insert(variation);
        placements.insert(epd);
        families[family]++;
        int ce = g->positions.back().white_move ? cp : -cp;  // EPD ce is from the side to move
        std::string moves;
        for(const std::string& m : g->uci_moves) moves += (moves.empty() ? "" : " ") + m;
        std::string name = l.eco + " " + l.name;
        for(size_t i; (i = name.find('"'))!=std::string::npos; ) name.erase(i, 1);
        out.push_back({l.eco + l.name, epd + " hmvc " + std::to_string(g->halfmove_clock) + "; fmvn " + std::to_string(g->fullmove())
                       + "; ce " + std::to_string(ce) + "; acd " + std::to_string(depth) + "; c0 \"" + name + "\"; c1 \"" + moves + "\";"});
        std::printf("%3zu %-60.60s cp %+d depth %d\n", out.size(), l.name.c_str(), cp, depth);
        std::fflush(stdout);
    }
    std::sort(out.begin(), out.end());

    std::ofstream f(argv[1]);
    f << "# OWNERSHIP=Claude\n"
      << "# Match opening suite, built by tools/make_openings.cpp from lichess-org/chess-openings.\n"
      << "# " << MIN_PLIES << "-" << MAX_PLIES << " plies, Lichess cloud eval |cp| <= " << MAX_CP << " (ce: side to move, acd: depth).\n";
    for(auto& o : out) f << o.second << "\n";
    std::printf("wrote %zu positions to %s\n", out.size(), argv[1]);
    for(auto& c : candidates) delete c.second;
    return (int)out.size()<count;
}
