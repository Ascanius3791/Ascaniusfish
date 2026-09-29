// OWNERSHIP=Claude
//
// Checks the Syzygy prober (lib/syzygy.hpp, issue #37):
//   1. memory: RSS before and after init() maps every table, and after a cold
//      probe of every table (which parses its header on first use);
//   2. cold and warm probe speed over the reference positions (cold: nothing
//      parsed yet and the files dropped from the page cache, posix_fadvise
//      DONTNEED);
//   3. the reference file (tools/syzygy_reference.txt, recorded from the Lichess
//      tablebase API): the WDL must match exactly, and so must the DTZ unless
//      probe_dtz() says it is rounded (the file stores that distance in moves).
//      Lichess serves the "no rounding" DTZ files, so there ours may be exactly
//      one ply short; with those files (a dir holding the standard .rtbw and the
//      3-4-5-dtz-nr .rtbz) every DTZ matches exactly;
//   4. consistency over random positions of every table: the WDL must be the best
//      outcome over the position's children (win / draw / loss; whether a win is
//      cursed depends on distance, which is what the next checks are for), each
//      child probed wherever it lands - the same table, a smaller one after a
//      capture, another after a promotion - and mate / stalemate when there is
//      no move. The DTZ must agree with the WDL (sign, and |dtz| > 100 exactly
//      for the cursed / blessed values) and with the children's DTZ: at most one
//      ply off, for the same rounding (SHOW_OFF=1 lists those positions);
//   5. "not found" before init(), and for castling rights or six pieces.
// Exit code 1 on any mismatch.
//
//   ./diagnostics/syzygy_test [tables_dir=~/syzygy] [random_per_table=300] [reference=tools/syzygy_reference.txt]
//   make syzygy-test [SYZYGY_PATH=~/syzygy] [SYZYGY_RANDOM=300]
//
// Build (from repo root, like any other diagnostic - see CLAUDE.md):
//   g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -pthread -o diagnostics/syzygy_test diagnostics/syzygy_test.cpp

#include "../lib/uci.hpp"
#include "../lib/syzygy.hpp"
#include "../tools/syzygy_positions.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

struct Ref { std::string fen; int wdl, dtz; bool precise; };

// Resident memory in kB: all, anonymous (heap), and file pages (the mappings).
struct Rss { long total = -1, anon = -1, file = -1; };
static Rss rss()
{
    Rss r;
    std::ifstream f("/proc/self/status");
    std::string line;
    while(std::getline(f, line))
    {
        if(line.rfind("VmRSS:", 0) == 0) r.total = std::atol(line.c_str() + 6);
        if(line.rfind("RssAnon:", 0) == 0) r.anon = std::atol(line.c_str() + 8);
        if(line.rfind("RssFile:", 0) == 0) r.file = std::atol(line.c_str() + 8);
    }
    return r;
}

static void print_rss(const char* when, const Rss& r, const Rss& base)
{
    std::printf("  RSS %-38s %6ld kB (+%ld kB: anon +%ld, file +%ld)\n", when, r.total, r.total - base.total,
                r.anon - base.anon, r.file - base.file);
}

// Drops the table files from the page cache (only pages no process maps).
static void evict(const std::string& dir)
{
    if(DIR* dp = opendir(dir.c_str()))
    {
        while(dirent* de = readdir(dp))
        {
            std::string f = de->d_name;
            if(f.size() < 5 || (f.substr(f.size()-5) != ".rtbw" && f.substr(f.size()-5) != ".rtbz"))
            continue;
            int fd = open((dir + "/" + f).c_str(), O_RDONLY);
            if(fd < 0) continue;
            posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            close(fd);
        }
        closedir(dp);
    }
}

static double seconds_since(std::chrono::steady_clock::time_point t)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

static int sign(int v) { return (v > 0) - (v < 0); }

static int g_failures = 0;
static void fail(const char* what, const std::string& fen, const std::string& detail)
{
    if(++g_failures <= 30)
    std::printf("  MISMATCH %s: %s  %s\n", what, fen.c_str(), detail.c_str());
}

static int material_count(const BB& pos)
{
    int n = 0;
    for(int i=0; i<12; i++) n += __builtin_popcountll(pos.Board[i]);
    return n;
}

static bool same_material(const BB& a, const BB& b)
{
    for(int i=0; i<12; i++)
    if(__builtin_popcountll(a.Board[i]) != __builtin_popcountll(b.Board[i])) return false;
    return true;
}

// Children by where they land, over the whole consistency check.
static long long g_child_same = 0, g_child_smaller = 0, g_child_promotion = 0;
static long long g_dtz_exact = 0, g_dtz_off_by_one = 0, g_mates = 0, g_stalemates = 0, g_ep_positions = 0;

static void check_consistency(const BB& pos, const std::string& fen)
{
    int wdl, dtz;
    if(!syzygy::probe_wdl(&pos, wdl) || !syzygy::probe_dtz(&pos, dtz))
    {
        fail("not found", fen, "");
        return;
    }
    if(pos.en_passant) g_ep_positions++;

    Move_List moves;
    const int total = generate_legal_moves(&pos, moves, GEN_ALL);
    int best = -2;                  // best outcome for the side to move: -1 / 0 / 1 (loss / draw / win)
    int expected_dtz = 0;           // from the children, 0 = none yet
    for(int k=0; k<total; k++)
    {
        const Move& m = moves[k];
        BB child;
        make_move(&pos, m, &child);
        int cw, cd;
        if(!syzygy::probe_wdl(&child, cw) || !syzygy::probe_dtz(&child, cd))
        {
            fail("child not found", fen, get_UCI(&pos, &child));
            return;
        }
        if(same_material(pos, child)) g_child_same++;
        else if(material_count(child) < material_count(pos)) g_child_smaller++;
        else g_child_promotion++;
        best = std::max(best, -sign(cw));

        // DTZ through this move, for the side to move
        const bool zeroing = material_count(child) < material_count(pos) || (pos.Board[pos.white_move ? 0 : 6] >> m.from & 1);
        int via;
        if(zeroing)
        via = cw == 2 ? -1 : cw == 1 ? -101 : cw == -1 ? 101 : cw == -2 ? 1 : 0;
        else if(cd == -1 && child.get_in_check() && count_legal_moves(&child, 1) == 0)
        via = 1;   // mate
        else
        via = cd == 0 ? 0 : -(cd + sign(cd));
        if(via == 0 || sign(via) != sign(dtz)) continue;
        // winning: the fastest; losing: the slowest
        if(expected_dtz == 0 || (dtz > 0 ? via < expected_dtz : via < expected_dtz))
        expected_dtz = via;
    }
    if(total == 0)
    {
        bool mate = pos.get_in_check();
        best = mate ? -1 : 0;
        (mate ? g_mates : g_stalemates)++;
        if(mate) expected_dtz = -1;
    }

    char buf[160];
    if(sign(wdl) != best)
    {
        std::snprintf(buf, sizeof buf, "wdl %d, best child outcome %d", wdl, best);
        fail("wdl vs children", fen, buf);
    }
    const bool dtz_agrees = wdl == 0 ? dtz == 0
                          : sign(dtz) == sign(wdl) && (std::abs(wdl) == 2 ? std::abs(dtz) <= 100 : std::abs(dtz) > 100);
    if(!dtz_agrees)
    {
        std::snprintf(buf, sizeof buf, "wdl %d dtz %d", wdl, dtz);
        fail("dtz vs wdl", fen, buf);
    }
    if(wdl != 0)
    {
        int diff = std::abs(dtz - expected_dtz);
        if(diff == 0) g_dtz_exact++;
        else if(diff == 1) { g_dtz_off_by_one++; if(getenv("SHOW_OFF")) std::printf("  off by one: %s wdl %d dtz %d children %d\n", fen.c_str(), wdl, dtz, expected_dtz); }
        else
        {
            std::snprintf(buf, sizeof buf, "dtz %d, from the children %d", dtz, expected_dtz);
            fail("dtz vs children", fen, buf);
        }
    }
}

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    const std::string dir = argc>1 ? argv[1] : std::string(getenv("HOME")) + "/syzygy";
    const int per_table = argc>2 ? std::atoi(argv[2]) : 300;
    const std::string ref_path = argc>3 ? argv[3] : "tools/syzygy_reference.txt";

    // --- reference positions --------------------------------------------------
    std::vector<Ref> refs;
    {
        std::ifstream in(ref_path);
        std::string line;
        while(std::getline(in, line))
        {
            if(line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string fen, w, d, p;
            std::getline(ss, fen, ';'); std::getline(ss, w, ';'); std::getline(ss, d, ';'); std::getline(ss, p, ';');
            refs.push_back({fen, std::atoi(w.c_str()), std::atoi(d.c_str()), p != "-"});
        }
    }
    std::vector<BB> ref_pos(refs.size());
    for(size_t i=0; i<refs.size(); i++)
    if(!uci_parse_fen(refs[i].fen, ref_pos[i]))
    {
        std::printf("bad FEN in %s: %s\n", ref_path.c_str(), refs[i].fen.c_str());
        return 1;
    }

    // --- 5a: nothing loaded -----------------------------------------------------
    {
        BB kqk;
        uci_parse_fen("4k3/8/8/8/8/8/8/4KQ2 w - - 0 1", kqk);
        int v;
        if(syzygy::probe_wdl(&kqk, v) || syzygy::probe_dtz(&kqk, v))
        fail("found before init", "4k3/8/8/8/8/8/8/4KQ2 w - - 0 1", "");
    }

    // --- 1 + 2: memory and cold probes ------------------------------------------
    std::printf("== memory and speed (%s)\n", dir.c_str());
    evict(dir);
    const Rss rss0 = rss();
    auto t0 = std::chrono::steady_clock::now();
    int n_wdl = syzygy::init(dir);
    double init_s = seconds_since(t0);
    const Rss rss1 = rss();
    std::printf("init: %d WDL + %d DTZ tables, max %d pieces, %.1f ms\n",
                n_wdl, syzygy::dtz_table_count(), syzygy::max_pieces(), init_s*1e3);
    print_rss("after init (every file mapped)", rss1, rss0);
    if(n_wdl == 0)
    {
        std::printf("no tables in %s\n", dir.c_str());
        return 1;
    }
    if(rss1.total - rss0.total >= 10*1024)
    fail("memory", "", "init grew RSS by 10 MB or more");

    // Cold: each pass starts with nothing parsed and the files out of the page
    // cache, so it includes parsing every table's header on its first probe.
    int v;
    t0 = std::chrono::steady_clock::now();
    for(const BB& b : ref_pos) syzygy::probe_wdl(&b, v);
    double cold_wdl = seconds_since(t0);
    print_rss("after a cold WDL probe of every table", rss(), rss0);
    syzygy::release();
    evict(dir);
    syzygy::init(dir);
    t0 = std::chrono::steady_clock::now();
    for(const BB& b : ref_pos) syzygy::probe_dtz(&b, v);
    double cold_dtz = seconds_since(t0);
    print_rss("after a cold DTZ probe of every table", rss(), rss0);

    const int rounds = 20;
    t0 = std::chrono::steady_clock::now();
    for(int r=0; r<rounds; r++) for(const BB& b : ref_pos) syzygy::probe_wdl(&b, v);
    double warm_wdl = seconds_since(t0);
    t0 = std::chrono::steady_clock::now();
    for(int r=0; r<rounds; r++) for(const BB& b : ref_pos) syzygy::probe_dtz(&b, v);
    double warm_dtz = seconds_since(t0);
    const double n = double(ref_pos.size());
    std::printf("probe_wdl: cold %.1f us, warm %.2f us per position (%zu reference positions)\n", cold_wdl/n*1e6, warm_wdl/(n*rounds)*1e6, ref_pos.size());
    std::printf("probe_dtz: cold %.1f us, warm %.2f us per position\n", cold_dtz/n*1e6, warm_dtz/(n*rounds)*1e6);

    // --- 3: reference ---------------------------------------------------------------
    std::printf("== reference: %s\n", ref_path.c_str());
    std::map<std::string, int> per_name;
    // A DTZ the file stores in moves (probe_dtz() says rounded) may be one ply
    // short of the API's exact value; anything else must match exactly.
    int ref_ok = 0, ref_exact = 0, ref_ep = 0, api_rounded = 0, ours_rounded = 0;
    for(size_t i=0; i<refs.size(); i++)
    {
        const Ref& r = refs[i];
        int w = 99, d = 99;
        bool rounded = false;
        bool fw = syzygy::probe_wdl(&ref_pos[i], w), fd = syzygy::probe_dtz(&ref_pos[i], d, &rounded);
        std::string name;
        for(int c=0; c<2; c++)
        {
            if(c) name += 'v';
            for(int t : {5, 4, 1, 3, 2, 0})
            name += std::string(__builtin_popcountll(ref_pos[i].Board[t + 6*c]), "PRNBQK"[t]);
        }
        per_name[name]++;
        ref_ep += ref_pos[i].en_passant != 0;
        api_rounded += !r.precise;
        ours_rounded += fd && rounded;
        const bool dtz_ok = d == r.dtz || (rounded && sign(d) == sign(r.dtz) && std::abs(r.dtz) == std::abs(d) + 1);
        if(!fw || !fd || w != r.wdl || !dtz_ok)
        {
            char buf[120];
            std::snprintf(buf, sizeof buf, "want wdl %d dtz %d, got wdl %d dtz %d%s%s", r.wdl, r.dtz,
                          fw ? w : 99, fd ? d : 99, rounded ? " (ours rounded)" : "", r.precise ? "" : " (API's rounded)");
            fail("reference", r.fen, buf);
        }
        else
        {
            ref_ok++;
            ref_exact += d == r.dtz;
        }
    }
    // the named side's material, whichever colour holds it: KQvKR and KRvKQ are one table
    std::map<std::string, int> tables_hit;
    for(auto& [name, count] : per_name)
    {
        std::string w = name.substr(0, name.find('v')), b = name.substr(name.find('v')+1);
        std::string a = w + "v" + b, s = b + "v" + w;
        tables_hit[std::ifstream(dir + "/" + a + ".rtbw") ? a : s] += count;
    }
    std::printf("%d / %zu match: WDL all, DTZ %d exactly and %d one ply short where the file stores moves\n",
                ref_ok, refs.size(), ref_exact, ref_ok - ref_exact);
    std::printf("(%d with an en passant square; %d of our DTZ rounded, %d of the API's); %zu of %d tables covered\n",
                ref_ep, ours_rounded, api_rounded, tables_hit.size(), n_wdl);
    if(refs.size() < 1000 || (int)tables_hit.size() < n_wdl)
    fail("reference coverage", "", "fewer than 1000 positions or a table without one");

    // --- 4: consistency over random positions ------------------------------------------
    std::printf("== consistency: %d random positions per table\n", per_table);
    std::mt19937_64 rng(37);
    long long checked = 0;
    t0 = std::chrono::steady_clock::now();
    for(const std::string& name : tb_table_names(dir))
    {
        int counts[12];
        if(!tb_material(name, counts)) continue;
        int before = g_failures;
        for(int k=0; k<per_table; k++)
        {
            // every 8th with an en passant square where the material allows one
            std::string fen = tb_random_fen(counts, k & 1, k & 2, k % 8 == 7, rng);
            if(fen.empty()) fen = tb_random_fen(counts, k & 1, k & 2, false, rng);
            if(fen.empty()) continue;
            BB pos;
            uci_parse_fen(fen, pos);
            check_consistency(pos, fen);
            checked++;
        }
        if(g_failures != before)
        std::printf("  %s: %d mismatches\n", name.c_str(), g_failures - before);
    }
    std::printf("%lld positions in %.1f s (%lld with an en passant square, %lld mates, %lld stalemates)\n",
                checked, seconds_since(t0), g_ep_positions, g_mates, g_stalemates);
    std::printf("children: %lld same table, %lld smaller table, %lld promotion\n", g_child_same, g_child_smaller, g_child_promotion);
    std::printf("dtz vs children: %lld exact, %lld one ply off\n", g_dtz_exact, g_dtz_off_by_one);

    // --- 5b: not found ----------------------------------------------------------------
    {
        const char* none[] = {
            "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1",          // castling rights, 3 pieces
            "r3k3/8/8/8/8/8/8/4K3 b q - 0 1",
            "4k3/8/8/8/8/8/PPP5/4K3 w - - 0 1",        // KPPPvK is in the set, 5 pieces...
            "4k3/pp6/8/8/8/8/PPP5/4K3 w - - 0 1",      // ...but 7 are not
            "4k3/p7/8/8/8/8/PPP5/4K3 w - - 0 1",       // 6 pieces
        };
        for(const char* fen : none)
        {
            BB pos;
            uci_parse_fen(fen, pos);
            int w, d;
            bool fw = syzygy::probe_wdl(&pos, w), fd = syzygy::probe_dtz(&pos, d);
            bool should = material_count(pos) <= 5 && !(pos.castle[0][0] || pos.castle[0][1] || pos.castle[1][0] || pos.castle[1][1]);
            if(fw != should || fd != should)
            fail("found / not found", fen, should ? "should be found" : "should not be found");
        }
    }

    std::printf("== %s (%d mismatches)\n", g_failures ? "FAILED" : "all correct", g_failures);
    return g_failures ? 1 : 0;
}
