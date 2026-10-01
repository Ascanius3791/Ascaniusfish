// OWNERSHIP=Claude
// tools/nne_data: the eval-correction net's dataset (issue #50, see
// docs/NNE_DESIGN.md "Data") from Lichess monthly dumps, read through zstdcat.
//
//   ./tools/nne_data run <dump.pgn.zst>... [out=data/nne] [jobs=6] [depth=6]
//                    [seed=50] [games=N] [syzygy=~/syzygy-nr]
//   ./tools/nne_data merge [out=data/nne]
//   ./tools/nne_data count <dump.pgn.zst>...
//   ./tools/nne_data relabel <data.tsv>... [out=data/nne-relabel] [jobs=6]
//                    [depth=6] [syzygy=~/syzygy-nr] [minutes=M]
//
// run: `jobs` forked workers each read every dump and take every jobs-th game
// (games are numbered from 0 across all dumps, in order). A game is kept when
// its Event is not Bullet, both players are rated 1500 or more, it starts from
// the normal position and has at least 50 plies. From a random ply after the
// first BOOK_PLIES the worker walks forward to the first quiet position (not in
// check, quiescence score == static eval) and labels it with a depth-`depth`
// search, run like "ucinewgame; position fen ...; go depth N" (fresh TT, the
// tables loaded). A label that is a mate or tablebase score drops the game.
// Every game a worker takes gets one line in out/parts/part-<w>.tsv, flushed
// at once, so an interrupted run started again with the same arguments carries
// on where it stopped. Then merge runs.
//
// merge: removes duplicate positions (the lowest game number keeps one),
// splits 80/10/10 by game (a hash of the Lichess game id) and writes
// out/{train,valid,test}.tsv and out/summary.txt: the counts, and the
// distribution of the correction label - static in the mover's view.
//
// count: only the game filter, no positions; how many games a dump gives.
//
// relabel (#56): the positions of existing dataset files (only their game,
// index, ply and FEN columns are read) labelled again by the current engine:
// static, qsearch, label and features are computed afresh, and a position that
// is no longer quiet, or whose label is now a mate or table score, is dropped.
// Worker w takes every jobs-th position in index order; part files, resuming
// and merge are the same as run's, so the output is a dataset like run's.
//
// minutes=M (run and relabel): every worker stops after M minutes, and the
// run exits with status 3 without merging; started again with the same
// arguments it carries on. That keeps one call inside a time limit.
//
// Output columns (tab separated, one header line starting with '#'):
//   game  index  ply  fen  static  qsearch  label  features
// game is the Lichess id, index the game number, ply the plies played before
// the position. static/qsearch/label are in centipawns, white's view (like
// every engine score); features are nne::active_features() of the FEN's
// position, comma separated.
#include "../lib/uci.hpp"
#include "../lib/nne.hpp"
#include "../gui/move_tree.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <csignal>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

constexpr int MIN_ELO = 1500;
constexpr int MIN_PLIES = 50;
constexpr int BOOK_PLIES = 16;      // the random ply is drawn after these
constexpr int MIN_PIECES = 6;       // fewer: the tables' result, not a search's

struct Options
{
    std::string command;
    std::vector<std::string> dumps;
    std::string out = "data/nne";
    int jobs = 6;
    int depth = 6;
    uint64_t seed = 50;
    long long games = -1;           // stop after this many games (-1: all)
    std::string syzygy = std::string(getenv("HOME") ? getenv("HOME") : "") + "/syzygy-nr";
    double minutes = 0;             // stop every worker after this long (0: never)
    bool out_given = false;
};

// A position of an existing dataset, for relabel.
struct Source_Position
{
    long long index;
    std::string game, fen;
    int ply;
};

// ---------------------------------------------------------------- reading

// The games of one dump, as text, through a zstdcat pipe.
class Dump_Reader
{
    public:
    explicit Dump_Reader(const std::string& path)
    {
        std::string cmd = "zstdcat -- '" + path + "'";
        pipe = popen(cmd.c_str(), "r");
    }
    ~Dump_Reader()
    {
        if(pipe) pclose(pipe);
        free(line);
    }
    bool ok() const { return pipe!=nullptr; }

    // The next game's text (tags and movetext); false at the end of the dump.
    bool next(std::string& game)
    {
        game.clear();
        if(!pending.empty())
        {
            game = pending;
            pending.clear();
        }
        ssize_t len;
        while((len = getline(&line, &cap, pipe)) > 0)
        {
            if(std::strncmp(line, "[Event ", 7)==0 && !game.empty())
            {
                pending.assign(line, len);
                return true;
            }
            game.append(line, len);
        }
        return !game.empty();
    }

    private:
    FILE* pipe = nullptr;
    char* line = nullptr;
    size_t cap = 0;
    std::string pending;
};

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

// SAN tokens in the movetext: no numbers, results, comments or NAGs.
int count_plies(const std::string& game)
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
        if(isdigit((unsigned char)token[0]) && token.find('.')!=std::string::npos)
        {
            size_t at = token.find_last_of('.');
            if(at+1>=token.size())
            continue;  // "12." or "12..." alone
        }
        plies++;
    }
    return plies;
}

// Why the game filter refuses a game, or nullptr when it takes it.
const char* filter(const std::string& game)
{
    if(!tag(game, "FEN").empty() || !tag(game, "SetUp").empty()
       || (!tag(game, "Variant").empty() && tag(game, "Variant")!="Standard"))
    return "setup";
    if(tag(game, "Event").find("Bullet")!=std::string::npos)
    return "bullet";
    std::string w = tag(game, "WhiteElo"), b = tag(game, "BlackElo");
    if(w.empty() || b.empty() || !isdigit((unsigned char)w[0]) || !isdigit((unsigned char)b[0])
       || std::atoi(w.c_str())<MIN_ELO || std::atoi(b.c_str())<MIN_ELO)
    return "elo";
    if(count_plies(game)<MIN_PLIES)
    return "short";
    return nullptr;
}

std::string game_id(const std::string& game)
{
    std::string site = tag(game, "Site");
    size_t slash = site.rfind('/');
    return slash==std::string::npos ? site : site.substr(slash+1);
}

uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x>>30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x>>27)) * 0x94D049BB133111EBULL;
    return x ^ (x>>31);
}

// ---------------------------------------------------------------- labelling

// One process's search state, like UCI_Engine's.
struct Labeller
{
    lookup_table* table = new lookup_table;
    BB* wfh = new BB[UCI_WFH_SIZE];
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable* cycle_table = new CuckooCycleTable;

    // "ucinewgame", "position fen", "go depth N": the last iteration's score,
    // white's view. Stops early at a mate, like the UCI search.
    int label(const BB& root, int depth)
    {
        table->reset();
        table->new_search();
        clear_killer_moves();
        path_history[0] = root;
        PV_Line pv;
        for(int d=1; d<=depth; d++)
        {
            pv = minimax(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, 0, cycle_table);
            if(pv.eval <= INT_MIN + max_mating_seq || pv.eval >= INT_MAX - max_mating_seq)
            break;
        }
        return pv.eval;
    }
};

bool is_mate_score(int e)
{
    return e <= INT_MIN + max_mating_seq || e >= INT_MAX - max_mating_seq;
}

int piece_count(const BB& pos)
{
    int n = 0;
    for(int i=0;i<12;i++)
    n += __builtin_popcountll(pos.Board[i]);
    return n;
}

// Not in check, a legal move, and the quiescence score equals the static eval.
bool quiet(BB& pos, Labeller& lab, int& stat, int& qs)
{
    if(pos.get_in_check())
    return false;
    BB children[MAX_LEGAL_MOVES];
    if(std::get<0>(all_moves(&pos, children))==0)
    return false;
    stat = eval(&pos, WEIGHTS_OG, 0);
    qs = minimax_tactical(&pos, lab.wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr).eval;
    return qs==stat;
}

// A quiet position's record: "ok" and its columns, or why it is dropped.
std::string label_record(const BB& pos, const std::string& id, int ply, const std::string& fen, int stat, int qs, const Options& opt, Labeller& lab)
{
    if(piece_count(pos)<MIN_PIECES)
    return "tb";

    auto t0 = std::chrono::steady_clock::now();
    int label = lab.label(pos, opt.depth);
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count();
    if(is_mate_score(label))
    return "mate";
    if(is_tb_score(label))
    return "tblabel";

    int feats[nne::MAX_ACTIVE];
    int n = nne::active_features(pos, feats);
    std::string f;
    for(int i=0;i<n;i++)
    {
        if(i) f += ',';
        f += std::to_string(feats[i]);
    }
    return "ok\t" + id + "\t" + std::to_string(ply) + "\t" + fen + "\t"
         + std::to_string(stat) + "\t" + std::to_string(qs) + "\t" + std::to_string(label) + "\t"
         + std::to_string(ms) + "\t" + f;
}

// One game a worker took, as its line in the part file (without the index).
std::string process_game(const std::string& game, long long index, const Options& opt, Labeller& lab, Move_Tree& tree)
{
    if(const char* why = filter(game))
    return std::string("filter\t") + why;
    std::string error;
    if(!tree.load_pgn(game, error))
    {
        std::fprintf(stderr, "game %lld (%s): %s\n", index, game_id(game).c_str(), error.c_str());
        return "bad";
    }
    std::vector<int> line = tree.path_ids();  // load_pgn leaves the cursor at the end
    int plies = (int)line.size()-1;
    if(plies<MIN_PLIES)
    return "filter\tshort";

    int start = BOOK_PLIES + (int)(splitmix64(opt.seed ^ splitmix64(index)) % (uint64_t)(plies-BOOK_PLIES+1));
    for(int ply=start; ply<=plies; ply++)
    {
        const Tree_Node& node = tree.node(line[ply]);
        std::string fen = Game::fen_of(node.pos, node.key[13], node.halfmove_clock, tree.fullmove_of(line[ply]));
        BB pos;
        if(!uci_parse_fen(fen, pos))  // the record is the FEN: label what it reads back as
        return "bad";
        int stat, qs;
        if(!quiet(pos, lab, stat, qs))
        continue;
        return label_record(pos, game_id(game), ply, fen, stat, qs, opt, lab);
    }
    return "noquiet";
}

// One relabelled position's line in the part file (without the index).
std::string process_position(const Source_Position& p, const Options& opt, Labeller& lab)
{
    BB pos;
    if(!uci_parse_fen(p.fen, pos))
    return "bad";
    int stat, qs;
    if(!quiet(pos, lab, stat, qs))
    return "noquiet";
    return label_record(pos, p.game, p.ply, p.fen, stat, qs, opt, lab);
}

std::string part_path(const Options& opt, int w)
{
    return opt.out + "/parts/part-" + std::to_string(w) + ".tsv";
}

// The last game index already in a part file; cuts off a line an interrupted
// run left half written.
long long resume_point(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
    return -1;
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t end = all.rfind('\n');
    size_t keep = end==std::string::npos ? 0 : end+1;
    if(keep<all.size() && truncate(path.c_str(), (off_t)keep)!=0)
    std::perror("truncate");
    if(keep==0)
    return -1;
    size_t begin = all.rfind('\n', keep-2);
    begin = begin==std::string::npos ? 0 : begin+1;
    return std::atoll(all.c_str()+begin);
}

constexpr int STOPPED = 3;  // a worker's exit status when minutes= ran out

bool out_of_time(const Options& opt, std::chrono::steady_clock::time_point t0)
{
    return opt.minutes>0 && std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count() > 60*opt.minutes;
}

int worker(int w, const Options& opt, const std::vector<Source_Position>& positions)
{
    std::string path = part_path(opt, w);
    long long done = resume_point(path);
    FILE* out = std::fopen(path.c_str(), "a");
    if(!out)
    {
        std::perror(path.c_str());
        return 1;
    }
    Labeller lab;
    Move_Tree* tree = new Move_Tree;
    long long index = -1, taken = 0, kept = 0;
    auto t0 = std::chrono::steady_clock::now();
    if(opt.command=="relabel")
    {
        for(size_t k=w; k<positions.size(); k+=opt.jobs)
        {
            if(positions[k].index<=done)
            continue;
            if(out_of_time(opt, t0))
            {
                std::fclose(out);
                return STOPPED;
            }
            std::string result = process_position(positions[k], opt, lab);
            std::fprintf(out, "%lld\t%s\n", positions[k].index, result.c_str());
            std::fflush(out);
            taken++;
            kept += result.compare(0, 3, "ok\t")==0;
            if(w==0 && taken%2000==0)
            {
                double s = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
                std::fprintf(stderr, "worker 0: %lld relabelled, %lld kept, %.0f s\n", taken, kept, s);
            }
        }
        std::fclose(out);
        return 0;
    }
    std::string game;
    for(const std::string& dump : opt.dumps)
    {
        Dump_Reader reader(dump);
        if(!reader.ok())
        {
            std::fprintf(stderr, "cannot read %s\n", dump.c_str());
            return 1;
        }
        while(reader.next(game))
        {
            index++;
            if(opt.games>=0 && index>=opt.games)
            break;
            if(index%opt.jobs!=w || index<=done)
            continue;
            if(out_of_time(opt, t0))
            {
                std::fclose(out);
                return STOPPED;
            }
            std::string result = process_game(game, index, opt, lab, *tree);
            std::fprintf(out, "%lld\t%s\n", index, result.c_str());
            std::fflush(out);
            taken++;
            kept += result.compare(0, 3, "ok\t")==0;
            if(w==0 && taken%2000==0)
            {
                double s = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
                std::fprintf(stderr, "worker 0: game %lld, %lld taken, %lld kept, %.0f s\n", index, taken, kept, s);
            }
        }
        if(opt.games>=0 && index>=opt.games)
        break;
    }
    std::fclose(out);
    return 0;
}

// ---------------------------------------------------------------- merging

struct Record
{
    long long index;
    std::string game, fen;
    int ply, stat, qs, label, ms;
    std::string features;
};

// The FEN without its clocks: what makes two positions the same.
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

// 0-7 train, 8 valid, 9 test: from the game id, so it holds whatever the order.
int split_of(const std::string& id)
{
    uint64_t h = 1469598103934665603ULL;
    for(char c : id)
    h = (h ^ (unsigned char)c) * 1099511628211ULL;
    return (int)(splitmix64(h) % 10);
}

double percentile(const std::vector<long long>& sorted, double p)
{
    if(sorted.empty())
    return 0;
    double at = p/100.0*(sorted.size()-1);
    size_t lo = (size_t)at;
    size_t hi = std::min(lo+1, sorted.size()-1);
    return sorted[lo] + (at-lo)*(sorted[hi]-sorted[lo]);
}

int merge(const Options& opt)
{
    std::map<std::string, long long> status;
    std::vector<Record> records;
    long long label_ms = 0;
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
            std::string key = f[1]=="filter" && f.size()>2 ? "filter " + f[2] : f[1];
            status[key]++;
            if(f[1]!="ok" || f.size()<10)
            continue;
            Record r;
            r.index = std::atoll(f[0].c_str());
            r.game = f[2];
            r.ply = std::atoi(f[3].c_str());
            r.fen = f[4];
            r.stat = std::atoi(f[5].c_str());
            r.qs = std::atoi(f[6].c_str());
            r.label = std::atoi(f[7].c_str());
            r.ms = std::atoi(f[8].c_str());
            r.features = f[9];
            label_ms += r.ms;
            records.push_back(std::move(r));
        }
    }
    if(status.empty())
    {
        std::fprintf(stderr, "no part files in %s/parts\n", opt.out.c_str());
        return 1;
    }
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.index<b.index; });

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
        std::fprintf(files[s], "#game\tindex\tply\tfen\tstatic\tqsearch\tlabel\tfeatures\n");
    }
    std::unordered_set<std::string> seen;
    long long counts[3] = {0, 0, 0}, duplicates = 0;
    std::vector<long long> correction, abs_correction;
    for(const Record& r : records)
    {
        if(!seen.insert(position_part(r.fen)).second)
        {
            duplicates++;
            continue;
        }
        int s = split_of(r.game);
        s = s<8 ? 0 : s==8 ? 1 : 2;
        counts[s]++;
        std::fprintf(files[s], "%s\t%lld\t%d\t%s\t%d\t%d\t%d\t%s\n", r.game.c_str(), r.index, r.ply,
                     r.fen.c_str(), r.stat, r.qs, r.label, r.features.c_str());
        bool white = r.fen.find(" w ")!=std::string::npos;
        long long c = nne::to_mover(r.label, white) - (long long)nne::to_mover(r.stat, white);
        correction.push_back(c);
        abs_correction.push_back(c<0 ? -c : c);
    }
    for(FILE* f : files)
    std::fclose(f);

    std::sort(correction.begin(), correction.end());
    std::sort(abs_correction.begin(), abs_correction.end());
    std::ostringstream s;
    s << "games taken by the workers, by outcome:\n";
    for(auto& [k, v] : status)
    s << "  " << k << ": " << v << "\n";
    s << "duplicate positions removed: " << duplicates << "\n";
    s << "positions: " << counts[0]+counts[1]+counts[2] << " (train " << counts[0] << ", valid "
      << counts[1] << ", test " << counts[2] << ")\n";
    s << "label search time, summed over workers: " << label_ms/1000 << " s\n\n";
    s << "correction = label - static, mover's view, cp:\n";
    s << "  percentile  correction  |correction|\n";
    for(double p : {0.1, 1.0, 5.0, 10.0, 25.0, 50.0, 75.0, 90.0, 95.0, 99.0, 99.9})
    {
        char buf[96];
        std::snprintf(buf, sizeof buf, "  %9.1f  %10.0f  %12.0f\n", p, percentile(correction, p), percentile(abs_correction, p));
        s << buf;
    }
    double sum = 0;
    long long over[4] = {0, 0, 0, 0};
    const long long limits[4] = {50, 200, 1000, 3000};
    for(long long c : correction)
    {
        sum += c;
        for(int i=0;i<4;i++)
        over[i] += (c<0 ? -c : c) > limits[i];
    }
    s << "  mean " << (correction.empty() ? 0 : sum/correction.size()) << "\n";
    for(int i=0;i<4;i++)
    s << "  |correction| > " << limits[i] << ": " << over[i] << " ("
      << (correction.empty() ? 0 : 100.0*over[i]/correction.size()) << "%)\n";
    std::string text = s.str();
    std::fputs(text.c_str(), stdout);
    std::ofstream(opt.out + "/summary.txt") << text;
    return 0;
}

// ---------------------------------------------------------------- counting

int count(const Options& opt)
{
    std::map<std::string, long long> status;
    long long games = 0;
    std::string game;
    for(const std::string& dump : opt.dumps)
    {
        Dump_Reader reader(dump);
        while(reader.next(game))
        {
            games++;
            const char* why = filter(game);
            status[why ? why : "kept"]++;
        }
    }
    std::printf("games: %lld\n", games);
    for(auto& [k, v] : status)
    std::printf("  %s: %lld\n", k.c_str(), v);
    return 0;
}

// ---------------------------------------------------------------- main

// relabel's input: the game, index, ply and FEN of every record, in index order.
bool read_positions(const Options& opt, std::vector<Source_Position>& positions)
{
    for(const std::string& path : opt.dumps)
    {
        std::ifstream in(path);
        if(!in)
        {
            std::fprintf(stderr, "cannot read %s\n", path.c_str());
            return false;
        }
        std::string line;
        while(std::getline(in, line))
        {
            if(line.empty() || line[0]=='#')
            continue;
            std::vector<std::string> f = split_tabs(line);
            if(f.size()<4)
            {
                std::fprintf(stderr, "%s: a line without game, index, ply and FEN\n", path.c_str());
                return false;
            }
            positions.push_back({std::atoll(f[1].c_str()), f[0], f[3], std::atoi(f[2].c_str())});
        }
    }
    std::sort(positions.begin(), positions.end(), [](const Source_Position& a, const Source_Position& b) { return a.index<b.index; });
    std::fprintf(stderr, "%zu positions to relabel\n", positions.size());
    return true;
}

// The arguments a part file was written with: a run that carries on must use
// the same, or its games would be numbered or labelled differently. Dumps may
// only be added at the end, which leaves the numbers of the games before alone.
std::string run_config(const Options& opt)
{
    std::string c = "jobs=" + std::to_string(opt.jobs) + " depth=" + std::to_string(opt.depth)
                  + " seed=" + std::to_string(opt.seed) + " tt=" + std::to_string(TT_EXPONENT_FOR_SIZE);
    for(const std::string& d : opt.dumps)
    c += " " + d.substr(d.rfind('/')+1);
    return opt.command=="relabel" ? "relabel " + c : c;
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
        if(!old.empty() && old!=config && config.compare(0, old.size()+1, old+" ")!=0)
        {
            std::fprintf(stderr, "%s holds a run with other arguments:\n  %s\nnow:\n  %s\n", config_path.c_str(), old.c_str(), config.c_str());
            return 1;
        }
        std::ofstream(config_path) << config << "\n";
    }
    if(!opt.syzygy.empty() && syzygy::init(opt.syzygy)==0)
    std::fprintf(stderr, "no tables in %s: labels are searched without them\n", opt.syzygy.c_str());

    std::vector<Source_Position> positions;
    if(opt.command=="relabel" && !read_positions(opt, positions))
    return 1;

    auto t0 = std::chrono::steady_clock::now();
    std::vector<pid_t> pids;
    for(int w=0; w<opt.jobs; w++)
    {
        pid_t pid = fork();
        if(pid==0)
        {
            // A worker outliving a killed run would still be appending to its
            // part file when the run is started again.
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if(getppid()==1)
            _exit(1);
            _exit(worker(w, opt, positions));
        }
        pids.push_back(pid);
    }
    bool failed = false, stopped = false;
    for(pid_t pid : pids)
    {
        int st = 0;
        waitpid(pid, &st, 0);
        bool stop = WIFEXITED(st) && WEXITSTATUS(st)==STOPPED;
        stopped |= stop;
        failed |= !stop && (!WIFEXITED(st) || WEXITSTATUS(st)!=0);
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    std::fprintf(stderr, "workers done in %.0f s wall\n", wall);
    if(failed)
    {
        std::fprintf(stderr, "a worker failed; run again to carry on\n");
        return 1;
    }
    if(stopped)
    {
        std::fprintf(stderr, "stopped after %g minutes; run again with the same arguments to carry on\n", opt.minutes);
        return STOPPED;
    }
    return merge(opt);
}

int main(int argc, char** argv)
{
    Options opt;
    if(argc<2)
    {
        std::fprintf(stderr, "usage: %s run|merge|count|relabel <dump.pgn.zst or data.tsv>... [out=] [jobs=] [depth=] [seed=] [games=] [syzygy=] [minutes=]\n", argv[0]);
        return 2;
    }
    opt.command = argv[1];
    for(int i=2;i<argc;i++)
    {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if(eq==std::string::npos) { opt.dumps.push_back(a); continue; }
        std::string k = a.substr(0, eq), v = a.substr(eq+1);
        if(k=="out") { opt.out = v; opt.out_given = true; }
        else if(k=="jobs") opt.jobs = std::max(1, std::atoi(v.c_str()));
        else if(k=="depth") opt.depth = std::max(1, std::atoi(v.c_str()));
        else if(k=="seed") opt.seed = std::strtoull(v.c_str(), nullptr, 10);
        else if(k=="games") opt.games = std::atoll(v.c_str());
        else if(k=="syzygy") opt.syzygy = v;
        else if(k=="minutes") opt.minutes = std::atof(v.c_str());
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }

    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    if(opt.command=="relabel" && !opt.out_given)
    opt.out = "data/nne-relabel";
    if(opt.command=="merge") return merge(opt);
    if(opt.dumps.empty()) { std::fprintf(stderr, "no dump given\n"); return 2; }
    if(opt.command=="relabel") return run(opt);
    if(opt.command=="count") return count(opt);
    if(opt.command=="run") return run(opt);
    std::fprintf(stderr, "unknown command %s\n", opt.command.c_str());
    return 2;
}
