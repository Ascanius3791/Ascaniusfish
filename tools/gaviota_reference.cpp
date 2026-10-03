// OWNERSHIP=Claude
// Records the Gaviota DTM reference (tools/gaviota_reference.txt) that
// diagnostics/gaviota_test.cpp checks our probe against (#77). One-off: the
// result is committed, this is kept so it can be rebuilt.
//
//   ./tools/gaviota_reference <in = tools/syzygy_reference.txt> <out.txt>
//
// Takes the Syzygy reference's positions (every 3-5 piece table, en passant
// included) and looks each up in the Lichess tablebase (tablebase.lichess.ovh,
// needs curl), whose DTM comes from the Gaviota tables.
//
// Output line: <fen>;<dtm>   side to move's view in plies: > 0 win, < 0 loss,
// 0 draw, -0 is written as "mated"; "-" where the API gives no DTM.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

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

// The DTM field of the position (not of its moves): "-" if null or missing,
// "" on an error.
static std::string lookup(const std::string& fen)
{
    std::string url_fen = fen;
    for(size_t i; (i = url_fen.find(' '))!=std::string::npos; ) url_fen.replace(i, 1, "_");
    for(int attempt=0; attempt<8; attempt++)
    {
        std::string out = run_capture("curl -s -m 30 -w '\\n%{http_code}' 'https://tablebase.lichess.ovh/standard?fen=" + url_fen + "'");
        std::string code = out.substr(out.rfind('\n')+1);
        if(code=="429") { std::fprintf(stderr, "rate limited, waiting 60 s\n"); std::this_thread::sleep_for(std::chrono::seconds(60)); continue; }
        if(code!="200") { std::this_thread::sleep_for(std::chrono::seconds(5)); continue; }
        std::string top = out.substr(0, out.find("\"moves\":"));  // the moves carry their own fields
        size_t i = top.find("\"dtm\":");
        if(i == std::string::npos || top.compare(i+6, 4, "null") == 0) return "-";
        const int dtm = std::atoi(top.c_str() + i + 6);
        if(dtm == 0 && top.find("\"checkmate\":true") != std::string::npos) return "mated";
        return std::to_string(dtm);
    }
    return "";
}

int main(int argc, char** argv)
{
    if(argc<3)
    {
        std::fprintf(stderr, "usage: %s <tools/syzygy_reference.txt> <out.txt>\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[1]);
    std::vector<std::string> fens;
    for(std::string line; std::getline(in, line); )
    if(!line.empty() && line[0]!='#')
    fens.push_back(line.substr(0, line.find(';')));
    std::vector<std::string> out;
    int no_dtm = 0;
    for(const std::string& fen : fens)
    {
        std::string dtm = lookup(fen);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if(dtm.empty()) { std::fprintf(stderr, "lookup failed: %s\n", fen.c_str()); return 1; }
        no_dtm += dtm=="-";
        out.push_back(fen + ";" + dtm);
        std::printf("%4zu/%zu %s  dtm %s\n", out.size(), fens.size(), fen.c_str(), dtm.c_str());
        std::fflush(stdout);
    }
    std::ofstream f(argv[2]);
    f << "# OWNERSHIP=Claude\n"
      << "# Gaviota DTM reference, recorded by tools/gaviota_reference.cpp from the\n"
      << "# Lichess tablebase API (tablebase.lichess.ovh/standard) on " << __DATE__ << ",\n"
      << "# for the positions of tools/syzygy_reference.txt.\n"
      << "# fen;dtm   (side to move's view in plies: > 0 win, < 0 loss, 0 draw, mated, - = no DTM)\n";
    for(const std::string& o : out) f << o << "\n";
    std::printf("wrote %zu positions to %s (%d without a DTM)\n", out.size(), argv[2], no_dtm);
    return 0;
}
