// OWNERSHIP=Claude
// Records the Syzygy reference positions (tools/syzygy_reference.txt) that
// diagnostics/syzygy_test.cpp checks our prober against. One-off: the result
// is committed, this is kept so it can be rebuilt or extended.
//
//   ./tools/syzygy_reference <tables dir> <out.txt> [per_table=7] [ep_per_table=4]
//
// For every .rtbw in the tables dir (only its name is used, never its contents)
// it makes per_table random legal positions of that material with a fixed seed,
// alternating which colour holds the named side's pieces and who is to move,
// plus ep_per_table positions with an en passant square where both sides have
// pawns. Each is looked up in the Lichess tablebase (tablebase.lichess.ovh,
// needs curl). A position the API refuses, or whose category is not one of the
// five exact ones (maybe-win and the like), is replaced by another.
//
// Output line: <fen>;<wdl>;<dtz>;<precise dtz or ->
#include "../lib/uci.hpp"
#include "syzygy_positions.hpp"
#include <cstdio>
#include <fstream>
#include <set>
#include <thread>

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

// The number after "key": in json, or false if it is missing or null.
static bool json_int(const std::string& json, const std::string& key, int& v)
{
    size_t i = json.find("\"" + key + "\":");
    if(i == std::string::npos) return false;
    i += key.size() + 3;
    if(json.compare(i, 4, "null") == 0) return false;
    v = std::atoi(json.c_str() + i);
    return true;
}

// 0 = found, 1 = refused or not exact (pick another position), -1 = error.
static int lookup(const std::string& fen, int& wdl, int& dtz, int& precise, bool& has_precise)
{
    std::string url_fen = fen;
    for(size_t i; (i = url_fen.find(' '))!=std::string::npos; ) url_fen.replace(i, 1, "_");
    for(int attempt=0; attempt<5; attempt++)
    {
        std::string out = run_capture("curl -s -w '\\n%{http_code}' 'https://tablebase.lichess.ovh/standard?fen=" + url_fen + "'");
        std::string code = out.substr(out.rfind('\n')+1);
        if(code=="400") return 1;
        if(code=="429") { std::fprintf(stderr, "rate limited, waiting 60 s\n"); std::this_thread::sleep_for(std::chrono::seconds(60)); continue; }
        if(code!="200") { std::this_thread::sleep_for(std::chrono::seconds(5)); continue; }
        std::string top = out.substr(0, out.find("\"moves\":"));  // the moves carry their own fields
        size_t c = top.find("\"category\":\"");
        if(c == std::string::npos) return -1;
        std::string cat = top.substr(c+12, top.find('"', c+12) - (c+12));
        if(cat=="win") wdl = 2;
        else if(cat=="cursed-win") wdl = 1;
        else if(cat=="draw") wdl = 0;
        else if(cat=="blessed-loss") wdl = -1;
        else if(cat=="loss") wdl = -2;
        else return 1;
        if(!json_int(top, "dtz", dtz)) return 1;
        has_precise = json_int(top, "precise_dtz", precise);
        return 0;
    }
    return -1;
}

int main(int argc, char** argv)
{
    if(argc<3)
    {
        std::fprintf(stderr, "usage: %s <tables dir> <out.txt> [per_table=7] [ep_per_table=4]\n", argv[0]);
        return 2;
    }
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    const int per_table = argc>3 ? std::atoi(argv[3]) : 7;
    const int ep_per_table = argc>4 ? std::atoi(argv[4]) : 4;
    std::vector<std::string> names = tb_table_names(argv[1]);
    if(names.empty())
    {
        std::fprintf(stderr, "no .rtbw files in %s\n", argv[1]);
        return 1;
    }
    std::mt19937_64 rng(20260930);
    std::set<std::string> seen;
    std::vector<std::string> out;
    int refused = 0;
    for(const std::string& name : names)
    {
        int counts[12];
        if(!tb_material(name, counts)) continue;
        for(int k=0; k<per_table+ep_per_table; k++)
        {
            const bool ep = k >= per_table;
            const bool swap = k & 1, white = (k >> 1) & 1;
            int wdl = 0, dtz = 0, precise = 0;
            bool has_precise = false;
            std::string fen;
            for(int tries=0; tries<20; tries++)
            {
                fen = tb_random_fen(counts, swap, white, ep, rng);
                if(fen.empty()) break;              // no en passant with this material
                if(seen.count(fen)) { fen.clear(); continue; }
                int r = lookup(fen, wdl, dtz, precise, has_precise);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                if(r < 0) { std::fprintf(stderr, "lookup failed: %s\n", fen.c_str()); return 1; }
                if(r == 0) break;
                refused++;
                fen.clear();
            }
            if(fen.empty()) continue;
            seen.insert(fen);
            out.push_back(fen + ";" + std::to_string(wdl) + ";" + std::to_string(dtz) + ";"
                          + (has_precise ? std::to_string(precise) : "-"));
            std::printf("%4zu %-8s %s  wdl %+d dtz %d\n", out.size(), name.c_str(), fen.c_str(), wdl, dtz);
            std::fflush(stdout);
        }
    }

    std::ofstream f(argv[2]);
    f << "# OWNERSHIP=Claude\n"
      << "# Syzygy reference positions, recorded by tools/syzygy_reference.cpp from the\n"
      << "# Lichess tablebase API (tablebase.lichess.ovh/standard) on " << __DATE__ << ".\n"
      << "# " << names.size() << " tables, " << per_table << " random positions each plus up to "
      << ep_per_table << " with an en passant square.\n"
      << "# fen;wdl;dtz;precise_dtz   (side to move's view, dtz in plies, - = the API's dtz is rounded)\n";
    for(const std::string& o : out) f << o << "\n";
    std::printf("wrote %zu positions to %s (%d refused or inexact, replaced)\n", out.size(), argv[2], refused);
    return 0;
}
