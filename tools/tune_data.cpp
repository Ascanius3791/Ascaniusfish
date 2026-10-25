// OWNERSHIP=Claude
// tools/tune_data: the weight tuner's position set (issue #86, docs/TUNING_PLAN.md
// "Game source" and "Balanced sampling") from the CCRL 40/15 archive.
//
//   ./tools/tune_data run <ccrl.pgn> [out=data/tune] [jobs=6] [seed=86] [games=N]
//                     [min_elo=3000] [min_plies=40] [gap=0] [per_game=3] [chain=0]
//   ./tools/tune_data select <ccrl.pgn> [out=data/tune] ...
//   ./tools/tune_data merge [out=data/tune]
//   ./tools/tune_data chain [out=data/tune] [jobs=6]
//   ./tools/tune_data pack <ccrl.pgn> [out=data/tune] [jobs=6] [pack=<out>/games.pack]
//
// The archive is bare PGN, 7-Zip-compressed: unpack it first (docs/TUNING_PLAN.md).
// The label of a position is only the result of its game, r in {1, 0.5, 0}, white's
// view; no engine's evaluation, none from the PGN either.
//
// select: one pass over the PGN, headers and move counts only. A game is a candidate
// when both engines are rated min_elo or more, the result is 1-0, 1/2-1/2 or 0-1, it
// has at least min_plies plies and (gap>0) the Elo gap is at most gap. Candidates are
// put in bands by the mean Elo of the two engines, 250 wide, the last open
// (3000-3249, 3250-3499, 3500+). In every band the three results are cut to the size
// of the smallest (black wins), by the games' hash, and written with their byte
// offsets to out/selected.tsv. So a band holds exactly a third per result in games.
//
// run: select, then `jobs` forked workers each take every jobs-th selected game, read
// it by its offset, and from each of per_game equal segments of the game after the
// first BOOK_PLIES plies take the first quiet position at or after a random ply in the
// segment (not in check, quiescence score == static eval, with at least MIN_PIECES
// pieces). Every game gets its lines in out/parts/part-<w>.tsv, flushed at once, so an
// interrupted run started again with the same arguments carries on. Then merge runs.
//
// merge: drops repeated positions (the lowest game number keeps one), splits 80/10/10
// by game (a hash of the game number, so no game has positions in two files) and, in
// every band and file, cuts the three results to the same number of positions (the
// lowest hashes stay). Writes out/{train,valid,test}.tsv and out/summary.txt: the
// counts per band, file and result, which are exactly equal in the three results.
//
// chain (#115; run ... chain=1 ends with it instead of merge): every position of
// every game, in game and ply order, to out/chain.tsv (columns game ply fen). No
// repeats dropped, no split, no cut: `tools/tune td= chain=` takes a game's positions
// as the bootstrap points between the positions it fits. jobs must be the run's.
//
// pack (#116): the data as one binary file (lib/game_pack.hpp) that tools/tune reads
// with data=<file>: every selected game that loads, all its moves, and a mark for
// every position of train/valid/test.tsv (with its line number) and of chain.tsv if
// there is one. No PGN or FEN in it. Every game is read back through the pack's own
// decoder and each mark's position checked against its FEN line (every field a FEN
// sets) before it is kept.
//
// Output columns (tab separated, one header line starting with '#'):
//   game  ply  fen  static  qsearch  result  band  white_elo  black_elo
// game is the number of the game in the archive (from 0), ply the plies played
// before the position, static/qsearch the current eval and quiescence score in
// centipawns (white's view), band the lower edge of the mean-Elo band.
#include "../lib/uci.hpp"
#include "../gui/move_tree.hpp"
#include "../lib/game_pack.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <csignal>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

constexpr int BOOK_PLIES = 24;      // CCRL books go up to 12 moves
constexpr int MIN_PIECES = 6;       // fewer: the tables decide, not the eval
constexpr int BAND_WIDTH = 250;
constexpr int BAND_TOP = 3500;      // from here on one open band

struct Options
{
    std::string command;
    std::string pgn;
    std::string out = "data/tune";
    int jobs = 6;
    uint64_t seed = 86;
    long long games = -1;           // stop reading after this many games (-1: all)
    int min_elo = 3000;
    int min_plies = 40;
    int gap = 0;                    // largest Elo gap (0: no limit)
    int per_game = 3;
    bool chain = false;             // run: write out/chain.tsv instead of merging
    std::string pack;               // pack: the output file (default out/games.pack)
};

struct Selected
{
    long long game, offset, length;
    int band, result;               // result: 0 = 0-1, 1 = draw, 2 = 1-0
    int white, black;
};

const char* const RESULT_NAMES[3] = { "0-1", "1/2-1/2", "1-0" };
const char* const RESULT_LABELS[3] = { "0", "0.5", "1" };

uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x>>30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x>>27)) * 0x94D049BB133111EBULL;
    return x ^ (x>>31);
}

int band_of(int mean_elo)
{
    return mean_elo>=BAND_TOP ? BAND_TOP : mean_elo/BAND_WIDTH*BAND_WIDTH;
}

std::string band_name(int band, int min_elo)
{
    int lo = std::max(band, min_elo/BAND_WIDTH*BAND_WIDTH);
    return band>=BAND_TOP ? std::to_string(band) + "+" : std::to_string(lo) + "-" + std::to_string(lo+BAND_WIDTH-1);
}

// ---------------------------------------------------------------- reading

std::string tag(const std::string& game, const char* name)
{
    std::string key = std::string("[") + name + " \"";
    size_t at = game.find(key);
    if(at==std::string::npos)
    return "";
    at += key.size();
    size_t end = game.find('"', at);
    return end==std::string::npos ? "" : game.substr(at, end-at);
}

// SAN tokens in the movetext: no numbers, results, comments or NAGs. With `sans`
// also the tokens themselves ("1.e4" as "e4").
int count_plies(const std::string& game, std::vector<std::string>* sans = nullptr)
{
    size_t i = game.find("\n\n");
    if(i==std::string::npos)
    return 0;
    int plies = 0, depth = 0;
    std::istringstream in(game.substr(i));
    std::string token;
    while(in >> token)
    {
        if(token[0]=='{') depth++;
        if(depth>0)
        {
            if(token.back()=='}') depth--;
            continue;
        }
        if(token[0]=='$' || token=="*" || token=="1-0" || token=="0-1" || token=="1/2-1/2")
        continue;
        size_t from = 0;
        if(isdigit((unsigned char)token[0]) && token.find('.')!=std::string::npos)
        {
            size_t at = token.find_last_of('.');
            if(at+1>=token.size())
            continue;
            from = at+1;
        }
        if(sans) sans->push_back(token.substr(from));
        plies++;
    }
    return plies;
}

void strip_cr(std::string& text)
{
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
}

// The games of the PGN with the byte offset and length of each one's text.
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
                return true;
            }
            if(game.empty())
            offset = at;
            game.append(line, len);
        }
        return !game.empty();
    }

    private:
    FILE* file = nullptr;
    char* line = nullptr;
    size_t cap = 0;
    long long position = 0, pending_offset = 0;
    std::string pending;
};

// ---------------------------------------------------------------- selecting

std::string selected_path(const Options& opt) { return opt.out + "/selected.tsv"; }

int select_games(const Options& opt)
{
    mkdir(opt.out.c_str(), 0755);
    Pgn_Reader reader(opt.pgn);
    if(!reader.ok())
    {
        std::fprintf(stderr, "cannot read %s\n", opt.pgn.c_str());
        return 1;
    }
    std::map<int, std::vector<Selected>> candidates[3];  // by result, then band
    std::map<std::string, long long> status;
    long long games = 0;
    std::string game;
    long long offset = 0;
    while(reader.next(game, offset))
    {
        if(opt.games>=0 && games>=opt.games)
        break;
        long long raw_length = (long long)game.size();
        strip_cr(game);  // the archive has CRLF line ends
        long long number = games++;
        std::string w = tag(game, "WhiteElo"), b = tag(game, "BlackElo"), r = tag(game, "Result");
        if(w.empty() || b.empty() || !isdigit((unsigned char)w[0]) || !isdigit((unsigned char)b[0]))
        {
            status["no elo"]++;
            continue;
        }
        int white = std::atoi(w.c_str()), black = std::atoi(b.c_str());
        if(white<opt.min_elo || black<opt.min_elo)
        {
            status["below min_elo"]++;
            continue;
        }
        int result = r=="1-0" ? 2 : r=="1/2-1/2" ? 1 : r=="0-1" ? 0 : -1;
        if(result<0)
        {
            status["no result"]++;
            continue;
        }
        if(opt.gap>0 && std::abs(white-black)>opt.gap)
        {
            status["elo gap"]++;
            continue;
        }
        if(!tag(game, "FEN").empty() || !tag(game, "SetUp").empty())
        {
            status["setup"]++;
            continue;
        }
        if(count_plies(game)<opt.min_plies)
        {
            status["short"]++;
            continue;
        }
        status["candidate"]++;
        int band = band_of((white+black)/2);
        candidates[result][band].push_back({number, offset, raw_length, band, result, white, black});
    }

    std::ostringstream s;
    s << "games read: " << games << "\n";
    for(auto& [k, v] : status)
    s << "  " << k << ": " << v << "\n";
    s << "\ncandidates and games selected per band (mean Elo of the two engines; both >= " << opt.min_elo << "):\n";
    s << "  band        candidates 1-0 / draw / 0-1      selected per result\n";
    std::vector<Selected> chosen;
    std::map<int, bool> bands;
    for(int r=0;r<3;r++)
    for(auto& kv : candidates[r])
    bands[kv.first] = true;
    for(auto& kv : bands)
    {
        int band = kv.first;
        size_t n = SIZE_MAX;
        for(int r=0;r<3;r++)
        n = std::min(n, candidates[r][band].size());
        char buf[160];
        std::snprintf(buf, sizeof buf, "  %-10s  %9zu %9zu %9zu      %zu\n", band_name(band, opt.min_elo).c_str(),
                      candidates[2][band].size(), candidates[1][band].size(), candidates[0][band].size(), n);
        s << buf;
        for(int r=0;r<3;r++)
        {
            std::vector<Selected>& v = candidates[r][band];
            std::sort(v.begin(), v.end(), [&](const Selected& a, const Selected& b)
            {
                return splitmix64(opt.seed ^ splitmix64(a.game)) < splitmix64(opt.seed ^ splitmix64(b.game));
            });
            chosen.insert(chosen.end(), v.begin(), v.begin()+n);
        }
    }
    std::sort(chosen.begin(), chosen.end(), [](const Selected& a, const Selected& b) { return a.game<b.game; });
    s << "selected games: " << chosen.size() << "\n";

    FILE* f = std::fopen(selected_path(opt).c_str(), "w");
    if(!f)
    {
        std::perror(selected_path(opt).c_str());
        return 1;
    }
    std::fprintf(f, "#game\toffset\tlength\tband\tresult\twhite\tblack\n");
    for(const Selected& c : chosen)
    std::fprintf(f, "%lld\t%lld\t%lld\t%d\t%d\t%d\t%d\n", c.game, c.offset, c.length, c.band, c.result, c.white, c.black);
    std::fclose(f);
    std::string text = s.str();
    std::fputs(text.c_str(), stdout);
    std::ofstream(opt.out + "/select_summary.txt") << text;
    return 0;
}

bool read_selected(const Options& opt, std::vector<Selected>& chosen)
{
    std::ifstream in(selected_path(opt));
    if(!in)
    return false;
    std::string line;
    while(std::getline(in, line))
    {
        if(line.empty() || line[0]=='#')
        continue;
        Selected c;
        if(std::sscanf(line.c_str(), "%lld\t%lld\t%lld\t%d\t%d\t%d\t%d", &c.game, &c.offset, &c.length, &c.band, &c.result, &c.white, &c.black)!=7)
        return false;
        chosen.push_back(c);
    }
    return true;
}

// ---------------------------------------------------------------- labelling

int piece_count(const BB& pos)
{
    int n = 0;
    for(int i=0;i<12;i++)
    n += __builtin_popcountll(pos.Board[i]);
    return n;
}

// Not in check, a legal move, and the quiescence score equals the static eval.
bool quiet(BB& pos, BB* wfh, int& stat, int& qs)
{
    if(pos.get_in_check())
    return false;
    BB children[MAX_LEGAL_MOVES];
    if(std::get<0>(all_moves(&pos, children))==0)
    return false;
    stat = eval(&pos, WEIGHTS_OG, 0);
    qs = minimax_tactical(&pos, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr).eval;
    return qs==stat;
}

// The lines of one game for its part file (without the selected index), "" if none
// could be taken; or "bad" when the PGN does not load.
std::string process_game(const std::string& text, const Selected& sel, const Options& opt, BB* wfh, Move_Tree& tree)
{
    std::string error;
    if(!tree.load_pgn(text, error))
    {
        std::fprintf(stderr, "game %lld: %s\n", sel.game, error.c_str());
        return "bad";
    }
    std::vector<int> line = tree.path_ids();
    int plies = (int)line.size()-1;
    std::string out;
    int lo = BOOK_PLIES, span = plies-lo+1;
    if(span<opt.per_game)
    return out;
    for(int k=0;k<opt.per_game;k++)
    {
        int seg_lo = lo + span*k/opt.per_game, seg_hi = lo + span*(k+1)/opt.per_game - 1;
        int start = seg_lo + (int)(splitmix64(opt.seed ^ splitmix64(sel.game*16+k)) % (uint64_t)(seg_hi-seg_lo+1));
        for(int ply=start; ply<=seg_hi; ply++)
        {
            const Tree_Node& node = tree.node(line[ply]);
            std::string fen = Game::fen_of(node.pos, node.key[13], node.halfmove_clock, tree.fullmove_of(line[ply]));
            BB pos;
            if(!uci_parse_fen(fen, pos))
            return "bad";
            if(piece_count(pos)<MIN_PIECES)
            break;
            int stat, qs;
            if(!quiet(pos, wfh, stat, qs))
            continue;
            out += std::to_string(ply) + "\t" + fen + "\t" + std::to_string(stat) + "\t" + std::to_string(qs) + "\n";
            break;
        }
    }
    return out;
}

std::string part_path(const Options& opt, int w)
{
    return opt.out + "/parts/part-" + std::to_string(w) + ".tsv";
}

// A game in a part file is "k pos ply fen static qs" lines and a closing "k end".
// Returns the last selected index with its closing line, and cuts off whatever an
// interrupted run left after it.
long long resume_point(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
    return -1;
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t keep = 0, at = 0;
    long long last = -1;
    while(at<all.size())
    {
        size_t nl = all.find('\n', at);
        if(nl==std::string::npos)
        break;
        size_t tab = all.find('\t', at);
        if(tab!=std::string::npos && tab<nl && all.compare(tab+1, 3, "end")==0 && nl==tab+4)
        {
            last = std::atoll(all.c_str()+at);
            keep = nl+1;
        }
        at = nl+1;
    }
    if(keep<all.size() && truncate(path.c_str(), (off_t)keep)!=0)
    std::perror("truncate");
    return last;
}

int worker(int w, const Options& opt, const std::vector<Selected>& chosen)
{
    std::string path = part_path(opt, w);
    long long done = resume_point(path);
    FILE* out = std::fopen(path.c_str(), "a");
    int fd = open(opt.pgn.c_str(), O_RDONLY);
    if(!out || fd<0)
    {
        std::perror(path.c_str());
        return 1;
    }
    BB* wfh = new BB[UCI_WFH_SIZE];
    Move_Tree* tree = new Move_Tree;
    long long taken = 0, positions = 0;
    auto t0 = std::chrono::steady_clock::now();
    std::string text;
    for(size_t k=w; k<chosen.size(); k+=opt.jobs)
    {
        if((long long)k<=done)
        continue;
        const Selected& sel = chosen[k];
        text.resize(sel.length);
        if(pread(fd, &text[0], sel.length, sel.offset)!=(ssize_t)sel.length)
        {
            std::fprintf(stderr, "game %lld: short read\n", sel.game);
            return 1;
        }
        strip_cr(text);
        std::string lines = process_game(text, sel, opt, wfh, *tree);
        std::string block;
        if(lines=="bad")
        block = std::to_string(k) + "\tbad\n";
        else
        {
            std::istringstream in(lines);
            std::string l;
            while(std::getline(in, l))
            {
                block += std::to_string(k) + "\tpos\t" + l + "\n";
                positions++;
            }
        }
        block += std::to_string(k) + "\tend\n";
        std::fwrite(block.data(), 1, block.size(), out);
        std::fflush(out);
        taken++;
        if(w==0 && taken%2000==0)
        {
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
            std::fprintf(stderr, "worker 0: %lld games, %lld positions, %.0f s\n", taken, positions, s);
        }
    }
    std::fclose(out);
    close(fd);
    return 0;
}

// ---------------------------------------------------------------- merging

struct Record
{
    long long k;            // index into the selected games
    int ply, stat, qs;
    std::string fen;
};

std::string position_part(const std::string& fen)
{
    size_t at = 0;
    for(int i=0;i<4 && at!=std::string::npos;i++)
    at = fen.find(' ', at+1);
    return fen.substr(0, at);
}

std::vector<std::string> split_tabs(const std::string& line)
{
    std::vector<std::string> f;
    size_t at = 0;
    for(;;)
    {
        size_t tab = line.find('\t', at);
        f.push_back(line.substr(at, tab==std::string::npos ? std::string::npos : tab-at));
        if(tab==std::string::npos)
        return f;
        at = tab+1;
    }
}

// 0-7 train, 8 valid, 9 test, from the game number.
int split_of(const Options& opt, long long game)
{
    return (int)(splitmix64(splitmix64(opt.seed + 1) ^ (uint64_t)game) % 10);
}

int merge(const Options& opt)
{
    std::vector<Selected> chosen;
    if(!read_selected(opt, chosen) || chosen.empty())
    {
        std::fprintf(stderr, "no %s: run select first\n", selected_path(opt).c_str());
        return 1;
    }
    std::vector<Record> records;
    long long bad = 0, finished = 0, empty_games = 0;
    std::vector<char> has_position(chosen.size(), 0);
    for(int w=0;;w++)
    {
        std::ifstream in(part_path(opt, w));
        if(!in)
        break;
        std::string line;
        while(std::getline(in, line))
        {
            std::vector<std::string> f = split_tabs(line);
            if(f.size()<2)
            continue;
            long long k = std::atoll(f[0].c_str());
            if(k<0 || k>=(long long)chosen.size())
            continue;
            if(f[1]=="bad") bad++;
            else if(f[1]=="end") finished++;
            else if(f[1]=="pos" && f.size()==6)
            {
                records.push_back({k, std::atoi(f[2].c_str()), std::atoi(f[4].c_str()), std::atoi(f[5].c_str()), f[3]});
                has_position[k] = 1;
            }
        }
    }
    if(finished==0)
    {
        std::fprintf(stderr, "no part files in %s/parts\n", opt.out.c_str());
        return 1;
    }
    for(size_t k=0;k<chosen.size();k++)
    empty_games += !has_position[k];
    std::sort(records.begin(), records.end(), [&](const Record& a, const Record& b)
    {
        return a.k!=b.k ? chosen[a.k].game<chosen[b.k].game : a.ply<b.ply;
    });

    // Repeated positions, then the cut to equal results per band and file.
    std::unordered_set<std::string> seen;
    long long duplicates = 0;
    std::map<std::tuple<int, int, int>, std::vector<const Record*>> groups;  // band, file, result
    std::map<int, bool> bands;
    for(const Record& r : records)
    {
        if(!seen.insert(position_part(r.fen)).second)
        {
            duplicates++;
            continue;
        }
        const Selected& s = chosen[r.k];
        int sp = split_of(opt, s.game);
        sp = sp<8 ? 0 : sp==8 ? 1 : 2;
        groups[{s.band, sp, s.result}].push_back(&r);
        bands[s.band] = true;
    }

    const char* names[3] = { "train", "valid", "test" };
    FILE* files[3];
    for(int s=0;s<3;s++)
    {
        std::string p = opt.out + "/" + names[s] + ".tsv";
        files[s] = std::fopen(p.c_str(), "w");
        if(!files[s])
        {
            std::perror(p.c_str());
            return 1;
        }
        std::fprintf(files[s], "#game\tply\tfen\tstatic\tqsearch\tresult\tband\twhite_elo\tblack_elo\n");
    }
    std::ostringstream s;
    s << "selected games: " << chosen.size() << ", loaded: " << finished << ", not loading: " << bad
      << ", without a quiet position: " << empty_games << "\n";
    s << "positions taken: " << records.size() << ", repeated positions removed: " << duplicates << "\n\n";
    s << "positions per band and file, 1-0 / draw / 0-1 (before the cut -> kept per result):\n";
    s << "  band        file     before the cut           kept per result\n";
    long long totals[3] = {0, 0, 0}, total_per_result[3] = {0, 0, 0};
    for(auto& bk : bands)
    for(int sp=0;sp<3;sp++)
    {
        size_t n = SIZE_MAX;
        size_t before[3];
        for(int r=0;r<3;r++)
        {
            before[r] = groups[{bk.first, sp, r}].size();
            n = std::min(n, before[r]);
        }
        for(int r=0;r<3;r++)
        {
            std::vector<const Record*>& v = groups[{bk.first, sp, r}];
            std::sort(v.begin(), v.end(), [&](const Record* a, const Record* b)
            {
                auto h = [&](const Record* x) { return splitmix64(opt.seed ^ splitmix64(std::hash<std::string>()(x->fen))); };
                return h(a)<h(b);
            });
            for(size_t i=0;i<n;i++)
            {
                const Record& rec = *v[i];
                const Selected& sel = chosen[rec.k];
                std::fprintf(files[sp], "%lld\t%d\t%s\t%d\t%d\t%s\t%d\t%d\t%d\n", sel.game, rec.ply, rec.fen.c_str(),
                             rec.stat, rec.qs, RESULT_LABELS[r], sel.band, sel.white, sel.black);
            }
            totals[sp] += n;
            total_per_result[r] += n;
        }
        char buf[200];
        std::snprintf(buf, sizeof buf, "  %-10s  %-6s  %7zu %7zu %7zu   %zu\n", band_name(bk.first, opt.min_elo).c_str(),
                      names[sp], before[2], before[1], before[0], n);
        s << buf;
    }
    for(FILE* f : files)
    std::fclose(f);
    s << "\npositions: " << totals[0]+totals[1]+totals[2] << " (train " << totals[0] << ", valid " << totals[1]
      << ", test " << totals[2] << ")\n";
    s << "per result: 1-0 " << total_per_result[2] << ", draw " << total_per_result[1] << ", 0-1 " << total_per_result[0] << "\n";
    std::string text = s.str();
    std::fputs(text.c_str(), stdout);
    std::ofstream(opt.out + "/summary.txt") << text;
    return 0;
}

// The parts hold every worker's games in selected order, and selected.tsv is in
// archive order, so reading part k%jobs for game k gives game and ply order.
int chain(const Options& opt)
{
    std::vector<Selected> chosen;
    if(!read_selected(opt, chosen) || chosen.empty())
    {
        std::fprintf(stderr, "no %s: run select first\n", selected_path(opt).c_str());
        return 1;
    }
    std::vector<std::ifstream> parts;
    for(int w=0;w<opt.jobs;w++)
    {
        parts.emplace_back(part_path(opt, w));
        if(!parts.back())
        {
            std::fprintf(stderr, "no %s\n", part_path(opt, w).c_str());
            return 1;
        }
    }
    const std::string path = opt.out + "/chain.tsv";
    FILE* out = std::fopen(path.c_str(), "w");
    if(!out)
    {
        std::perror(path.c_str());
        return 1;
    }
    std::fputs("#game\tply\tfen\n", out);
    long long positions = 0, games = 0;
    std::string line;
    for(size_t k=0;k<chosen.size();k++)
    {
        std::ifstream& in = parts[k%opt.jobs];
        bool ended = false;
        while(!ended && std::getline(in, line))
        {
            std::vector<std::string> f = split_tabs(line);
            if(f.size()<2 || std::atoll(f[0].c_str())!=(long long)k)
            {
                std::fprintf(stderr, "%s: game %zu expected, got %s\n", part_path(opt, (int)(k%opt.jobs)).c_str(), k, line.c_str());
                return 1;
            }
            ended = f[1]=="end";
            if(f[1]=="pos" && f.size()==6)
            {
                std::fprintf(out, "%lld\t%s\t%s\n", chosen[k].game, f[2].c_str(), f[3].c_str());
                positions++;
            }
        }
        if(!ended)
        {
            std::fprintf(stderr, "game %zu has no end line: run again to finish it\n", k);
            return 1;
        }
        games++;
    }
    std::fclose(out);
    std::fprintf(stderr, "%s: %lld positions of %lld games\n", path.c_str(), positions, games);
    return 0;
}

// ---------------------------------------------------------------- packing

struct Pack_Line
{
    long long game;
    Pack_Mark mark;
    Packed_Pos expect;      // the position its FEN line gives
    int fen = -1;           // a chain line: its FEN in the block, parsed by the thread that packs the game
};

// FEN -> the Packed_Pos of uci_parse_fen()'s BB; false if the FEN is bad.
bool expected_pos(const std::string& fen, Packed_Pos& out)
{
    BB b;
    return uci_parse_fen(fen, b) && pack_position(b, out);
}

// The split files' lines as marks, sorted by game and ply.
bool read_split_marks(const Options& opt, std::vector<Pack_Line>& lines, uint64_t counts[PACK_SETS])
{
    for(int set=PACK_TRAIN; set<=PACK_TEST; set++)
    {
        const std::string path = opt.out + "/" + PACK_SET_NAMES[set] + ".tsv";
        std::ifstream in(path);
        if(!in)
        {
            std::fprintf(stderr, "no %s: run merge first\n", path.c_str());
            return false;
        }
        std::string line;
        uint32_t index = 0;
        while(std::getline(in, line))
        {
            if(line.empty() || line[0]=='#')
            continue;
            std::vector<std::string> f = split_tabs(line);
            Pack_Line L{std::atoll(f[0].c_str()), {set, f.size()>2 ? std::atoi(f[1].c_str()) : -1, index++}, {}};
            if(f.size()<3 || !expected_pos(f[2], L.expect))
            {
                std::fprintf(stderr, "%s: bad line %s\n", path.c_str(), line.c_str());
                return false;
            }
            lines.push_back(L);
        }
        counts[set] = index;
    }
    std::sort(lines.begin(), lines.end(), [](const Pack_Line& a, const Pack_Line& b)
    {
        return a.game!=b.game ? a.game<b.game : a.mark.ply<b.mark.ply;
    });
    return true;
}

// The legal move `san` names in pos (list: its moves), or -1 if none or several do.
// Check marks and annotations are ignored. Much cheaper than Move_Tree::load_pgn(),
// which builds the SAN of every legal move; the pack checks the result against the
// FEN lines anyway.
int san_move(const BB& pos, const std::string& token, const Move_List& list, int n)
{
    std::string s = token;
    while(!s.empty() && std::strchr("+#!?", s.back())) s.pop_back();
    const int base = pos.white_move ? 0 : 56, own = pos.white_move ? 0 : 6;
    uint64_t mine = 0;
    for(int i=0;i<6;i++) mine |= pos.Board[i+own];
    int found = -1;
    auto take = [&](int i) { if(found>=0) return false; found = i; return true; };
    if(s=="O-O" || s=="0-0" || s=="O-O-O" || s=="0-0-0")
    {
        const bool king_side = s.size()==3;
        for(int i=0;i<n;i++)    // all_moves() castling: the king's first step's square to its last, nothing of ours on it
        if(!(mine>>list[i].from & 1) && list[i].from==base+(king_side ? 5 : 3) && list[i].to==base+(king_side ? 6 : 2) && !take(i))
        return -1;
        return found;
    }
    const char* PIECES = "PRNBQK";      // the Board order, as piece_on() % 6
    int piece = 0, promotion = -1;
    size_t at = 0;
    if(!s.empty() && std::strchr("RNBQK", s[0])) piece = std::strchr(PIECES, s[at++])-PIECES;
    if(s.size()>=2 && std::strchr("RNBQ", s.back()))
    {
        promotion = std::strchr(PIECES, s.back())-PIECES;
        s.pop_back();
        if(s.back()=='=') s.pop_back();
    }
    if(s.size()<at+2) return -1;
    const char f = s[s.size()-2], r = s[s.size()-1];
    if(f<'a' || f>'h' || r<'1' || r>'8') return -1;
    const int to = (f-'a') + 8*(r-'1');
    int from_file = -1, from_rank = -1;
    for(size_t k=at; k+2<s.size(); k++)
    {
        if(s[k]>='a' && s[k]<='h') from_file = s[k]-'a';
        else if(s[k]>='1' && s[k]<='8') from_rank = s[k]-'1';
        else if(s[k]!='x') return -1;
    }
    for(int i=0;i<n;i++)
    {
        const Move& m = list[i];
        if(m.to!=to || !(pos.Board[piece+own]>>m.from & 1)) continue;
        if((from_file>=0 && m.from%8!=from_file) || (from_rank>=0 && m.from/8!=from_rank)) continue;
        const bool promotes = piece==0 && (to<8 || to>=56);
        if(promotes ? m.promotion_piece_type!=promotion : promotion>=0) continue;
        if(!take(i)) return -1;
    }
    return found;
}

// One game's record into `record` ("" if its PGN does not load), its marks [m0, m1)
// by ply. False on a game the pack cannot hold or that does not decode to its lines.
bool pack_game(const std::string& text, const Selected& sel, const Pack_Line* m0, const Pack_Line* m1,
               std::string& record, std::string& error)
{
    record.clear();
    const std::string name = "game " + std::to_string(sel.game);
    std::vector<std::string> sans;
    count_plies(text, &sans);
    std::vector<uint8_t> ranks;
    BB pos, next;
    uci_parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", pos);
    for(const std::string& san : sans)
    {
        Move_List list;
        const int n = generate_legal_moves(&pos, list, GEN_ALL);
        const int i = san_move(pos, san, list, n);
        if(i<0)
        return true;            // does not load
        int rank = 0;
        for(int k=0;k<n;k++) rank += move_key(list[k])<move_key(list[i]);
        ranks.push_back((uint8_t)rank);
        make_move(&pos, list[i], &next);
        pos = next;
    }
    if(ranks.empty())
    return true;
    if(ranks.size()>65535)
    {
        error = name + " is too long";
        return false;
    }
    if(ranks.size()>65535)
    {
        error = name + " is too long";
        return false;
    }
    std::vector<Pack_Mark> marks;
    for(const Pack_Line* m=m0; m<m1; m++)
    {
        if(m->mark.ply<0 || m->mark.ply>(int)ranks.size())
        {
            error = name + ": a mark at ply " + std::to_string(m->mark.ply) + " of " + std::to_string(ranks.size());
            return false;
        }
        marks.push_back(m->mark);
    }
    append_game(record, (uint32_t)sel.game, sel.white, sel.black, sel.result, ranks, marks);

    // read it back as tools/tune will: the marks come in the order of [m0, m1)
    size_t k = 0;
    bool same = true;
    if(!replay(read_game((const uint8_t*)record.data()), (1u<<PACK_SETS)-1, [&](const Pack_Mark&, const BB& pos)
    {
        Packed_Pos got;
        same = same && k<marks.size() && pack_position(pos, got) && got==m0[k].expect;
        k++;
    }, error))
    return false;
    if(!same || k!=marks.size())
    {
        error = name + ": its positions do not decode to their FEN lines";
        return false;
    }
    return true;
}

int pack(const Options& opt)
{
    std::vector<Selected> chosen;
    if(!read_selected(opt, chosen) || chosen.empty())
    {
        std::fprintf(stderr, "no %s: run select first\n", selected_path(opt).c_str());
        return 1;
    }
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count(); };
    std::vector<Pack_Line> splits;
    uint64_t want[PACK_SETS] = {0, 0, 0, 0};
    if(!read_split_marks(opt, splits, want))
    return 1;
    std::fprintf(stderr, "split lines: train %llu, valid %llu, test %llu (%.0f s)\n",
                 (unsigned long long)want[0], (unsigned long long)want[1], (unsigned long long)want[2], secs());
    const std::string chain_path = opt.out + "/chain.tsv";
    std::ifstream chain_in(chain_path);
    std::fprintf(stderr, chain_in ? "chain: %s\n" : "no %s: the pack gets no chain marks\n", chain_path.c_str());
    const int fd = open(opt.pgn.c_str(), O_RDONLY);
    if(fd<0)
    {
        std::perror(opt.pgn.c_str());
        return 1;
    }
    const std::string path = opt.pack.empty() ? opt.out + "/games.pack" : opt.pack;
    std::string dir = opt.out;
    while(dir.size()>1 && dir.back()=='/') dir.pop_back();
    const std::string tag = "tools/tune_data pack of " + dir.substr(dir.rfind('/')+1) + ": the selected games of "
                          + opt.pgn.substr(opt.pgn.rfind('/')+1) + ", marks from train/valid/test.tsv"
                          + (chain_in ? " and chain.tsv" : "");
    Pack_Writer writer;
    std::string error;
    if(!writer.open(path, tag, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    // blocks of games in archive order: the block's chain lines, then its games in jobs threads
    const size_t BLOCK = 16384;
    size_t next_split = 0, unloaded = 0, packed = 0;
    std::string line;
    bool pending = false;       // `line` is a chain line of a later block
    long long last_chain_game = -1;
    for(size_t k0=0; k0<chosen.size(); k0+=BLOCK)
    {
        const size_t k1 = std::min(chosen.size(), k0+BLOCK);
        std::vector<long long> chain_game;
        std::vector<std::string> chain_rest;   // "ply\tfen"
        while(pending || (chain_in && std::getline(chain_in, line)))
        {
            pending = false;
            if(line.empty() || line[0]=='#')
            continue;
            const long long g = std::atoll(line.c_str());
            if(g<last_chain_game)
            {
                std::fprintf(stderr, "%s is not in game order at %s\n", chain_path.c_str(), line.c_str());
                return 1;
            }
            if(g>chosen[k1-1].game)
            {
                pending = true;
                break;
            }
            last_chain_game = g;
            chain_game.push_back(g);
            chain_rest.push_back(line.substr(line.find('\t')+1));
        }
        // every game's marks: its split lines, then its chain lines (sorted by ply in the thread)
        std::vector<std::vector<Pack_Line>> marks(k1-k0);
        size_t c = 0;
        for(size_t k=k0; k<k1; k++)
        {
            const long long g = chosen[k].game;
            for(; next_split<splits.size() && splits[next_split].game<=g; next_split++)
            {
                if(splits[next_split].game<g)
                {
                    std::fprintf(stderr, "game %lld of the split files is not a selected game\n", splits[next_split].game);
                    return 1;
                }
                marks[k-k0].push_back(splits[next_split]);
            }
            for(; c<chain_game.size() && chain_game[c]<=g; c++)
            {
                if(chain_game[c]<g)
                {
                    std::fprintf(stderr, "game %lld of %s is not a selected game\n", chain_game[c], chain_path.c_str());
                    return 1;
                }
                Pack_Line L{g, {PACK_CHAIN, std::atoi(chain_rest[c].c_str()), 0}, {}};
                L.fen = (int)c;
                marks[k-k0].push_back(L);
            }
        }
        std::vector<std::string> records(k1-k0), errors(opt.jobs);
        std::vector<size_t> bad(opt.jobs, 0);
        std::vector<std::thread> pool;
        for(int t=0;t<opt.jobs;t++)
        pool.emplace_back([&, t]
        {
            std::string text, err;
            for(size_t k=k0+t; k<k1; k+=opt.jobs)
            {
                std::vector<Pack_Line>& m = marks[k-k0];
                for(Pack_Line& L : m)
                if(L.fen>=0)
                {
                    const std::string& r = chain_rest[L.fen];
                    if(!expected_pos(r.substr(r.find('\t')+1), L.expect)) { errors[t] = "bad line in " + chain_path + ": " + r; return; }
                }
                std::stable_sort(m.begin(), m.end(), [](const Pack_Line& a, const Pack_Line& b) { return a.mark.ply<b.mark.ply; });
                const Selected& sel = chosen[k];
                text.resize(sel.length);
                if(pread(fd, &text[0], sel.length, sel.offset)!=(ssize_t)sel.length) { errors[t] = "game " + std::to_string(sel.game) + ": short read"; return; }
                strip_cr(text);
                if(!pack_game(text, sel, m.data(), m.data()+m.size(), records[k-k0], err)) { errors[t] = err; return; }
                if(records[k-k0].empty())
                {
                    if(!m.empty()) { errors[t] = "game " + std::to_string(sel.game) + " has marks but its PGN does not load"; return; }
                    bad[t]++;
                }
            }
        });
        for(auto& th : pool) th.join();
        for(int t=0;t<opt.jobs;t++)
        if(!errors[t].empty())
        {
            std::fprintf(stderr, "%s\n", errors[t].c_str());
            return 1;
        }
        std::string block;
        uint64_t counts[PACK_SETS] = {0, 0, 0, 0};
        size_t games = 0;
        for(size_t k=k0; k<k1; k++)
        {
            if(records[k-k0].empty()) continue;
            block += records[k-k0];
            games++;
            for(const Pack_Line& L : marks[k-k0]) counts[L.mark.set]++;
        }
        for(int t=0;t<opt.jobs;t++) unloaded += bad[t];
        packed += games;
        want[PACK_CHAIN] += chain_game.size();
        if(!writer.add(block, games, counts))
        break;
        if(k0/BLOCK%8==7) std::fprintf(stderr, "%zu of %zu games, %.0f s\n", k1, chosen.size(), secs());
    }
    close(fd);
    if(pending)
    {
        std::fprintf(stderr, "%s has games after the last selected one\n", chain_path.c_str());
        return 1;
    }
    if(next_split!=splits.size())
    {
        std::fprintf(stderr, "game %lld of the split files is not a selected game\n", splits[next_split].game);
        return 1;
    }
    if(!writer.close(error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    Game_Pack check;
    if(!check.open(path, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    for(int s=0;s<PACK_SETS;s++)
    if(check.marks(s)!=want[s])
    {
        std::fprintf(stderr, "%s: %llu %s marks, the files have %llu\n", path.c_str(), (unsigned long long)check.marks(s),
                     PACK_SET_NAMES[s], (unsigned long long)want[s]);
        return 1;
    }
    struct stat st;
    stat(path.c_str(), &st);
    std::printf("%s: %zu games (%zu more do not load), marks train %llu valid %llu test %llu chain %llu, %.1f MB, %.0f s\n",
                path.c_str(), packed, unloaded, (unsigned long long)want[0], (unsigned long long)want[1],
                (unsigned long long)want[2], (unsigned long long)want[3], st.st_size/1e6, secs());
    return 0;
}

// ---------------------------------------------------------------- main

std::string run_config(const Options& opt)
{
    return "jobs=" + std::to_string(opt.jobs) + " seed=" + std::to_string(opt.seed) + " games=" + std::to_string(opt.games)
         + " min_elo=" + std::to_string(opt.min_elo) + " min_plies=" + std::to_string(opt.min_plies)
         + " gap=" + std::to_string(opt.gap) + " per_game=" + std::to_string(opt.per_game) + " pgn=" + opt.pgn;
}

int run(const Options& opt)
{
    mkdir(opt.out.c_str(), 0755);
    std::string parts = opt.out + "/parts";
    mkdir(parts.c_str(), 0755);
    std::string config_path = parts + "/config.txt";
    std::string config = run_config(opt);
    {
        std::ifstream in(config_path);
        std::string old;
        std::getline(in, old);
        if(!old.empty() && old!=config)
        {
            std::fprintf(stderr, "%s holds a run with other arguments:\n  %s\nnow:\n  %s\n", config_path.c_str(), old.c_str(), config.c_str());
            return 1;
        }
        std::ofstream(config_path) << config << "\n";
    }
    if(select_games(opt)!=0)
    return 1;
    std::vector<Selected> chosen;
    if(!read_selected(opt, chosen))
    return 1;

    auto t0 = std::chrono::steady_clock::now();
    std::vector<pid_t> pids;
    for(int w=0; w<opt.jobs; w++)
    {
        pid_t pid = fork();
        if(pid==0)
        {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if(getppid()==1)
            _exit(1);
            _exit(worker(w, opt, chosen));
        }
        pids.push_back(pid);
    }
    bool failed = false;
    for(pid_t pid : pids)
    {
        int st = 0;
        waitpid(pid, &st, 0);
        failed |= !WIFEXITED(st) || WEXITSTATUS(st)!=0;
    }
    std::fprintf(stderr, "workers done in %.0f s wall\n", std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count());
    if(failed)
    {
        std::fprintf(stderr, "a worker failed; run again to carry on\n");
        return 1;
    }
    return opt.chain ? chain(opt) : merge(opt);
}

int main(int argc, char** argv)
{
    Options opt;
    if(argc<2)
    {
        std::fprintf(stderr, "usage: %s run|select|merge|chain|pack <ccrl.pgn> [out=] [jobs=] [seed=] [games=] [min_elo=] [min_plies=] [gap=] [per_game=] [chain=] [pack=]\n", argv[0]);
        return 2;
    }
    opt.command = argv[1];
    for(int i=2;i<argc;i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq==std::string::npos) { opt.pgn = a; continue; }
        std::string k = a.substr(0, eq), v = a.substr(eq+1);
        if(k=="out") opt.out = v;
        else if(k=="jobs") opt.jobs = std::max(1, std::atoi(v.c_str()));
        else if(k=="seed") opt.seed = std::strtoull(v.c_str(), nullptr, 10);
        else if(k=="games") opt.games = std::atoll(v.c_str());
        else if(k=="min_elo") opt.min_elo = std::atoi(v.c_str());
        else if(k=="min_plies") opt.min_plies = std::atoi(v.c_str());
        else if(k=="gap") opt.gap = std::atoi(v.c_str());
        else if(k=="per_game") opt.per_game = std::max(1, std::atoi(v.c_str()));
        else if(k=="chain") opt.chain = std::atoi(v.c_str())!=0;
        else if(k=="pack") opt.pack = v;
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }

    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    if(opt.command=="merge") return merge(opt);
    if(opt.command=="chain") return chain(opt);
    if(opt.pgn.empty()) { std::fprintf(stderr, "no PGN given\n"); return 2; }
    if(opt.command=="select") return select_games(opt);
    if(opt.command=="run") return run(opt);
    if(opt.command=="pack") return pack(opt);
    std::fprintf(stderr, "unknown command %s\n", opt.command.c_str());
    return 2;
}
