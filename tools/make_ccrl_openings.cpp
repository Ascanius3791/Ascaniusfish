// OWNERSHIP=Claude
// tools/make_ccrl_openings: the opening suite of the SPRT (#92), from finished CCRL
// games only. No engine evaluation anywhere: an opening is balanced by its games' results.
//
//   ./tools/make_ccrl_openings <ccrl.pgn> [out=tools/openings_ccrl.epd] [min_ply=12]
//                              [max_ply=16] [min_games=20] [max_dev=0.1] [min_plies=40]
//                              [seed=92]
//
// The archive (data/ccrl/ccrl4040.pgn) is bare PGN once unpacked (docs/TUNING_PLAN.md).
// One pass reads every game that has a result, starts from the start position and is at
// least min_plies plies long, and takes its positions after min_ply..max_ply plies (the
// book part: CCRL books go up to 12 moves), each with the game's result. Positions are
// grouped by what the repetition rule compares (pieces, side to move, castling, a legal
// en passant). A group is kept when it has at least min_games games and white scored
// within max_dev of 50% in them. A kept position that a game reached through another kept
// one is dropped, so no opening is in the suite twice, not even as a continuation of
// another. The rest is shuffled by seed (a match that stops early has played a random
// sample, not one opening family) and written as EPD:
//   <fen> hmvc H; fmvn F; c0 "<ECO opening, variation>"; c1 "<uci moves>"; games N; wscore S;
// with the name, moves and halfmove clock of the first game that reached it. Also printed:
// how many positions other min_games/max_dev would keep. Single-threaded, one read of the
// PGN, then the kept games are read again by their offsets.
#include "game_rules.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static const char* const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

struct Options
{
    std::string pgn;
    std::string out = "tools/openings_ccrl.epd";
    int min_ply = 12, max_ply = 16;
    int min_games = 20;
    double max_dev = 0.1;
    int min_plies = 40;
    uint64_t seed = 92;
};

// One position of one game. `id` is the position's hash until the grouping, its group after.
struct Seen
{
    uint64_t id;
    uint32_t game;          // index into the offsets of the games read
    uint8_t ply;
    uint8_t white2;         // white's points times 2 in that game
};

struct Group
{
    uint32_t games = 0, white2 = 0;
};

[[noreturn]] static void die(const std::string& msg)
{
    std::fprintf(stderr, "make_ccrl_openings: %s\n", msg.c_str());
    std::exit(2);
}

static uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x>>30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x>>27)) * 0x94D049BB133111EBULL;
    return x ^ (x>>31);
}

static uint64_t hash_key(const Position_Key& key)
{
    uint64_t h = 0;
    for(uint64_t k : key)
    h = splitmix64(h ^ k);
    return h;
}

static std::string tag(const std::string& game, const char* name)
{
    std::string key = std::string("[") + name + " \"";
    size_t at = game.find(key);
    if(at==std::string::npos)
    return "";
    at += key.size();
    size_t end = game.find('"', at);
    return end==std::string::npos ? "" : game.substr(at, end-at);
}

// The games of the PGN, each with the byte offset of its text.
class Pgn_Reader
{
    public:
    explicit Pgn_Reader(const std::string& path) { file = std::fopen(path.c_str(), "rb"); }
    ~Pgn_Reader()
    {
        if(file) std::fclose(file);
        free(line);
    }
    bool ok() const { return file!=nullptr; }

    bool next(std::string& game, long long& offset)
    {
        game.clear();
        if(!pending.empty())
        {
            game = pending;
            offset = pending_offset;
            pending.clear();
        }
        ssize_t len;
        while((len = getline(&line, &cap, file)) > 0)
        {
            long long at = position;
            position += len;
            if(std::strncmp(line, "[Event ", 7)==0 && !game.empty())
            {
                pending.assign(line, len);
                pending_offset = at;
                game.erase(std::remove(game.begin(), game.end(), '\r'), game.end());
                return true;
            }
            if(game.empty())
            offset = at;
            game.append(line, len);
        }
        game.erase(std::remove(game.begin(), game.end(), '\r'), game.end());  // the archive has CRLF lines
        return !game.empty();
    }

    // The game that starts at `offset`.
    std::string at(long long offset)
    {
        pending.clear();
        if(fseeko(file, offset, SEEK_SET)!=0)
        return "";
        position = offset;
        std::string game;
        long long ignored;
        next(game, ignored);
        return game;
    }

    private:
    FILE* file = nullptr;
    char* line = nullptr;
    size_t cap = 0;
    long long position = 0, pending_offset = 0;
    std::string pending;
};

// The SAN moves of the movetext: no move numbers, results, comments or NAGs.
// False on a variation, which CCRL games don't have.
static bool san_tokens(const std::string& game, std::vector<std::string>& out)
{
    out.clear();
    size_t i = game.find("\n\n");
    if(i==std::string::npos)
    return false;
    int comment = 0;
    std::istringstream in(game.substr(i));
    std::string token;
    while(in >> token)
    {
        if(token[0]=='{') comment++;
        if(comment>0)
        {
            if(token.back()=='}') comment--;
            continue;
        }
        if(token[0]=='(')
        return false;
        if(token[0]=='$' || token=="*" || token=="1-0" || token=="0-1" || token=="1/2-1/2")
        continue;
        if(isdigit((unsigned char)token[0]))  // "12." or "12...", or "12.Nf3" glued
        {
            size_t dot = token.find_last_of('.');
            if(dot==std::string::npos)
            continue;
            token = token.substr(dot+1);
            if(token.empty())
            continue;
        }
        out.push_back(token);
    }
    return true;
}

// Plays SAN `token` in `game`; false if no legal move has that SAN. Only the moves to
// the token's target square are given a SAN, which is what keeps one pass over a few
// million games quick.
static bool play_san(Game& game, std::string token)
{
    while(!token.empty() && std::strchr("+#!?", token.back())) token.pop_back();
    if(token=="0-0") token = "O-O";
    if(token=="0-0-0") token = "O-O-O";
    if(token.size()>=3 && std::strchr("QRBN", token.back()) && isdigit((unsigned char)token[token.size()-2]))
    token.insert(token.size()-1, "=");  // "e8Q" -> "e8=Q"
    bool castle = token[0]=='O';
    std::string target;
    if(!castle)
    {
        std::string body = token.substr(0, token.find('='));
        if(body.size()<2)
        return false;
        target = body.substr(body.size()-2);
    }
    const BB& pos = game.positions.back();
    for(int k=0;k<game.n_children;k++)
    {
        std::string uci = get_UCI(&pos, game.children+k);
        if(!castle && uci.compare(2, 2, target)!=0)
        continue;
        std::string s = san(pos, game.children, game.n_children, k);
        while(s.back()=='+' || s.back()=='#') s.pop_back();
        if(s==token)
        return game.play(uci);
    }
    return false;
}

static void read_options(int argc, char** argv, Options& o)
{
    if(argc<2)
    {
        std::fprintf(stderr, "usage: %s <ccrl.pgn> [out=tools/openings_ccrl.epd] [min_ply=12] [max_ply=16]\n"
                             "       [min_games=20] [max_dev=0.1] [min_plies=40] [seed=92]\n", argv[0]);
        std::exit(2);
    }
    o.pgn = argv[1];
    for(int i=2;i<argc;i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq==std::string::npos)
        die("expected key=value, got " + a);
        std::string k = a.substr(0, eq), v = a.substr(eq+1);
        if(v.empty()) continue;
        if(k=="out") o.out = v;
        else if(k=="min_ply") o.min_ply = std::atoi(v.c_str());
        else if(k=="max_ply") o.max_ply = std::atoi(v.c_str());
        else if(k=="min_games") o.min_games = std::atoi(v.c_str());
        else if(k=="max_dev") o.max_dev = std::atof(v.c_str());
        else if(k=="min_plies") o.min_plies = std::atoi(v.c_str());
        else if(k=="seed") o.seed = std::strtoull(v.c_str(), nullptr, 10);
        else die("unknown option " + k);
    }
    if(o.min_ply<1 || o.max_ply<o.min_ply || o.max_ply>200)
    die("need 1 <= min_ply <= max_ply <= 200");
    if(o.min_games<1 || o.max_dev<0)
    die("need min_games >= 1 and max_dev >= 0");
}

// Which groups survive min_games/max_dev and the continuation rule. `seen` is sorted by
// game, then ply. With `first` it also gives each survivor's first record (lowest game).
static std::vector<char> survivors(const std::vector<Seen>& seen, const std::vector<Group>& groups,
                                   int min_games, double max_dev, std::vector<int64_t>* first)
{
    size_t n = groups.size();
    std::vector<char> kept(n, 0), dropped(n, 0);
    for(size_t g=0;g<n;g++)
    kept[g] = (int)groups[g].games>=min_games
           && std::fabs(groups[g].white2/(2.0*groups[g].games)-0.5)<=max_dev+1e-12;
    for(size_t i=0;i<seen.size();)
    {
        size_t end = i;
        int64_t earlier = -1;  // the first kept position of this game
        for(; end<seen.size() && seen[end].game==seen[i].game; end++)
        {
            int64_t g = (int64_t)seen[end].id;
            if(!kept[g])
            continue;
            if(earlier>=0 && g!=earlier)  // not a repetition of the same position
            dropped[g] = 1;
            if(earlier<0)
            earlier = g;
        }
        i = end;
    }
    for(size_t g=0;g<n;g++)
    kept[g] = kept[g] && !dropped[g];
    if(first)
    {
        first->assign(n, -1);
        for(size_t i=0;i<seen.size();i++)
        if(kept[seen[i].id] && (*first)[seen[i].id]<0)
        (*first)[seen[i].id] = (int64_t)i;
    }
    return kept;
}

int main(int argc, char** argv)
{
    Options o;
    read_options(argc, argv, o);

    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    Pgn_Reader reader(o.pgn);
    if(!reader.ok())
    die("cannot open " + o.pgn + " (unpack the CCRL archive first, docs/TUNING_PLAN.md)");
    BB start;
    uci_parse_fen(START_FEN, start);

    std::vector<Seen> seen;
    std::vector<long long> offsets;
    long long total = 0, no_result = 0, setup = 0, short_games = 0, unreadable = 0;
    Game* game = new Game;
    std::string text;
    std::vector<std::string> tokens;
    long long offset = 0;
    while(reader.next(text, offset))
    {
        total++;
        if(total%100000==0)
        {
            std::fprintf(stderr, "\r%lld games, %zu positions", total, seen.size());
            std::fflush(stderr);
        }
        std::string result = tag(text, "Result");
        int white2 = result=="1-0" ? 2 : result=="1/2-1/2" ? 1 : result=="0-1" ? 0 : -1;
        if(white2<0) { no_result++; continue; }
        if(!tag(text, "FEN").empty()) { setup++; continue; }
        if(!san_tokens(text, tokens)) { unreadable++; continue; }
        if((int)tokens.size()<std::max(o.min_plies, o.max_ply)) { short_games++; continue; }
        game->start(start, 0, 1);
        bool ok = true;
        for(int p=0;p<o.max_ply && ok;p++)
        ok = play_san(*game, tokens[p]);
        if(!ok) { unreadable++; continue; }
        uint32_t index = (uint32_t)offsets.size();
        offsets.push_back(offset);
        for(int p=o.min_ply;p<=o.max_ply;p++)
        seen.push_back({hash_key(game->keys[p]), index, (uint8_t)p, (uint8_t)white2});
    }
    std::fprintf(stderr, "\r%lld games read: %zu used, %lld without a result, %lld from a set-up position, "
                         "%lld shorter than %d plies, %lld unreadable\n",
                 total, offsets.size(), no_result, setup, short_games, std::max(o.min_plies, o.max_ply), unreadable);
    if(offsets.empty())
    die("no usable games in " + o.pgn);

    // Group by position; from here on a record's id is its group.
    std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.id<b.id; });
    std::vector<Group> groups;
    for(size_t i=0;i<seen.size();)
    {
        uint64_t key = seen[i].id;
        Group g;
        size_t end = i;
        for(; end<seen.size() && seen[end].id==key; end++)
        {
            g.games++;
            g.white2 += seen[end].white2;
        }
        for(size_t j=i;j<end;j++)
        seen[j].id = groups.size();
        groups.push_back(g);
        i = end;
    }
    std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b)
              { return a.game!=b.game ? a.game<b.game : a.ply<b.ply; });
    std::printf("%zu distinct positions after %d-%d plies\n\n", groups.size(), o.min_ply, o.max_ply);

    // What other thresholds would keep, so the defaults can be judged without a rerun.
    const int GAMES[] = {10, 20, 40, 80};
    const double DEVS[] = {0.03, 0.05, 0.075, 0.1, 0.15};
    std::printf("positions kept   max_dev:");
    for(double d : DEVS) std::printf(" %7.3f", d);
    std::printf("\n");
    for(int mg : GAMES)
    {
        std::printf("  min_games %4d        ", mg);
        for(double d : DEVS)
        {
            std::vector<char> k = survivors(seen, groups, mg, d, nullptr);
            std::printf(" %7ld", (long)std::count(k.begin(), k.end(), 1));
        }
        std::printf("\n");
    }

    std::vector<int64_t> first;
    std::vector<char> kept = survivors(seen, groups, o.min_games, o.max_dev, &first);
    std::vector<uint32_t> chosen;
    for(size_t g=0;g<groups.size();g++)
    if(kept[g]) chosen.push_back((uint32_t)g);
    std::printf("\nmin_games %d, max_dev %g: %zu positions\n", o.min_games, o.max_dev, chosen.size());
    if(chosen.empty())
    die("nothing kept; lower min_games or raise max_dev");

    // Read the first game of every kept position again, in file order.
    std::sort(chosen.begin(), chosen.end(), [&](uint32_t a, uint32_t b) { return first[a]<first[b]; });
    struct Line { uint64_t order; std::string epd; };
    std::vector<Line> lines;
    for(uint32_t g : chosen)
    {
        const Seen& s = seen[first[g]];
        std::string text = reader.at(offsets[s.game]);
        if(!san_tokens(text, tokens))
        die("cannot read the game at byte " + std::to_string(offsets[s.game]) + " again");
        game->start(start, 0, 1);
        for(int p=0;p<s.ply;p++)
        if(!play_san(*game, tokens[p]))
        die("cannot replay the game at byte " + std::to_string(offsets[s.game]));
        std::istringstream fen(game->fen());
        std::string field, epd;
        for(int i=0;i<4 && fen>>field;i++) epd += (i ? " " : "") + field;
        std::string name = tag(text, "ECO");
        std::string opening = tag(text, "Opening"), variation = tag(text, "Variation");
        if(!opening.empty()) name += (name.empty() ? "" : " ") + opening;
        if(!variation.empty()) name += ", " + variation;
        std::string moves;
        for(const std::string& m : game->uci_moves) moves += (moves.empty() ? "" : " ") + m;
        char stats[96];
        std::snprintf(stats, sizeof stats, " games %u; wscore %.3f;", groups[g].games, groups[g].white2/(2.0*groups[g].games));
        epd += " hmvc " + std::to_string(game->halfmove_clock) + "; fmvn " + std::to_string(game->fullmove())
             + "; c0 \"" + name + "\"; c1 \"" + moves + "\";" + stats;
        lines.push_back({splitmix64(o.seed ^ (uint64_t)s.game<<8 ^ s.ply), epd});
    }
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.order<b.order; });

    std::ofstream out(o.out);
    if(!out)
    die("cannot write " + o.out);
    out << "# OWNERSHIP=Claude\n"
        << "# SPRT opening suite (#92), built by tools/make_ccrl_openings from " << o.pgn << " (CCRL 40/15):\n"
        << "# positions after " << o.min_ply << "-" << o.max_ply << " plies with >= " << o.min_games
        << " games and white's score within " << o.max_dev << " of 50%, none a continuation of another,\n"
        << "# shuffled (seed " << o.seed << "). Balanced by game results only, no engine eval. games/wscore: those games.\n";
    for(const Line& l : lines)
    out << l.epd << "\n";
    std::printf("%zu positions written to %s\n", lines.size(), o.out.c_str());
    delete game;
    return 0;
}
