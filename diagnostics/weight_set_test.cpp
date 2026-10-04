// OWNERSHIP=Claude
// Weight sets (#85, lib/weight_set.hpp). Checks:
//  1. weights/w1.txt reads back to WEIGHTS_OG, every field, and
//     write_weight_set(WEIGHTS_OG) is the file byte for byte;
//  2. the compiled-in default (lib/weights_default.hpp) is that file, not a stale copy;
//  3. a table's numbers go to the squares the format says (a8 first, h1 last);
//  4. a broken file is refused, naming what is wrong, and leaves W as it was;
//  5. with every piece-square table scrambled (no longer rank-symmetric),
//     basic_eval() stays colour-symmetric: black reads the tables rank-flipped.
// Exit code 1 on any failure.
//
// Build and run from the repo root:
//   g++ -O3 -mpopcnt -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/weight_set_test diagnostics/weight_set_test.cpp
//   ./diagnostics/weight_set_test
#include "../lib/uci.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void expect(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if(!ok)
    failures++;
}

static bool same(const WEIGHTS& a, const WEIGHTS& b)
{
    return std::memcmp(&a, &b, sizeof(WEIGHTS))==0;
}

static std::string read_file(const std::string& path)
{
    std::ifstream in(path);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// `text` with the first `from` replaced by `to`.
static std::string replaced(std::string text, const std::string& from, const std::string& to)
{
    const size_t at = text.find(from);
    if(at!=std::string::npos)
    text.replace(at, from.size(), to);
    return text;
}

// Ranks flipped, colours swapped, side to move swapped (as in king_safety_test.cpp).
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
    for(char ch : std::string("KQkq"))
    {
        char from = std::isupper((unsigned char)ch) ? std::tolower(ch) : std::toupper(ch);
        if(castling.find(from) != std::string::npos)
        c += ch;
    }
    if(ep != "-")
    ep[1] = ep[1] == '3' ? '6' : '3';
    return out + " " + (side == "w" ? "b" : "w") + " " + c + " " + ep + " 0 1";
}

static void round_trip(const std::string& file)
{
    // every field of the set junk first, so a field the file leaves out shows
    WEIGHTS W = WEIGHTS_OG;
    Weight_Field fields[MAX_WEIGHT_FIELDS];
    const int n = weight_fields(W, fields);
    for(int f=0;f<n;f++)
    for(int k=0;k<fields[f].count;k++)
    fields[f].values[k] = 12345;
    W.version = 0;
    Weight_Set_Info info;
    std::string error;
    const bool ok = load_weight_set("weights/w1.txt", W, info, error);
    expect(ok, "weights/w1.txt reads" + (ok ? std::string() : ": " + error));
    expect(same(W, WEIGHTS_OG), "weights/w1.txt is WEIGHTS_OG, every field (" + std::to_string(n) + " keys)");
    expect(W.version==1 && info.parent==0, "it is set 1, without a parent");
    expect(write_weight_set(WEIGHTS_OG, Weight_Set_Info()) == file, "write_weight_set(WEIGHTS_OG) is weights/w1.txt byte for byte");
    expect(std::string(WEIGHTS_DEFAULT_TEXT) == "\n" + file && std::string(WEIGHTS_DEFAULT_FILE) == "weights/w1.txt",
           "lib/weights_default.hpp holds weights/w1.txt (else: make lib/weights_default.hpp)");
}

static void squares(const std::string& file)
{
    // the knight's opening table: its first number is a8, its last h1
    const std::string head = "piece_table_value_opening knight\n";
    std::string text = file;
    const size_t at = text.find(head);
    if(at==std::string::npos)
    {
        expect(false, "no knight opening table in weights/w1.txt");
        return;
    }
    const size_t first = at + head.size();
    text.replace(first, 5, "  999");
    const size_t last = text.find('\n', first + 7*41) - 5;// 8 rows of 8 five-wide numbers
    text.replace(last, 5, "  777");
    WEIGHTS W = WEIGHTS_OG;
    Weight_Set_Info info;
    std::string error;
    const bool ok = read_weight_set(text, W, info, error);
    expect(ok && W.piece_table_value_opening[2][56]==999 && W.piece_table_value_opening[2][7]==777,
           "a table's first number is a8 (square 56), its last h1 (square 7)" + (ok ? std::string() : ": " + error));
}

static void broken(const std::string& file)
{
    struct Case { std::string text, what, message; };
    const std::vector<Case> cases = {
        {replaced(file, "ks_hit 69\n", ""), "a missing field", "missing 'ks_hit'"},
        {replaced(file, "ks_hit 69", "ks_hitx 69"), "an unknown key", "unknown key 'ks_hitx'"},
        {file + "ks_hit 3\n", "a field given twice", "'ks_hit' given twice"},
        {replaced(file, "ks_hit 69", "ks_hit 6x9"), "a word for a number", "'ks_hit' needs 1 numbers"},
        {replaced(file, "ks_shelter 40 40 0 12 28 38 38 38", "ks_shelter 40 40 0 12 28 38 38"), "a number short", "'ks_shelter' needs 8 numbers"},
        {replaced(file, "version 1\n", ""), "no version", "no version"},
        {replaced(file, "version 1\n", "version 0\n"), "version 0", "version is not a number >= 1"},
        {replaced(file, "ks_danger_div 5000", "ks_danger_div 0"), "a zero divisor", "must be > 0"},
        {replaced(file, "piece_value 100", "piece_value 99999999999"), "a number out of range", "'piece_value' needs 6 numbers"},
        {file.substr(0, file.find("piece_table_value_endgame king") + 60), "a file cut inside a table", "needs 64 numbers"},
    };
    for(const Case& c : cases)
    {
        WEIGHTS W = WEIGHTS_OG;
        Weight_Set_Info info;
        std::string error;
        const bool ok = read_weight_set(c.text, W, info, error);
        expect(!ok && error.find(c.message)!=std::string::npos && same(W, WEIGHTS_OG),
               c.what + " is refused: " + (ok ? std::string("read") : error));
    }
    WEIGHTS W = WEIGHTS_OG;
    Weight_Set_Info info;
    std::string error;
    const bool refused = !load_weight_set("weights/no_such_set.txt", W, info, error);
    expect(refused && same(W, WEIGHTS_OG), "a missing file is refused: " + error);
}

static void symmetry()
{
    // every table entry moved by a different amount, so no table is rank-symmetric
    WEIGHTS W = WEIGHTS_OG;
    unsigned rng = 2026;
    for(int p=0;p<6;p++)
    for(int sq=0;sq<64;sq++)
    {
        rng = rng*1103515245u + 12345u;
        W.piece_table_value_opening[p][sq] += (int)(rng>>16)%61 - 30;
        rng = rng*1103515245u + 12345u;
        W.piece_table_value_endgame[p][sq] += (int)(rng>>16)%61 - 30;
    }
    std::ifstream epd("tools/openings.epd");
    std::string line;
    std::vector<BB> positions;
    BB* wfh = new BB[256];
    while(std::getline(epd, line) && positions.size()<4000)
    {
        std::istringstream in(line);
        std::string a, b, c, d;
        if(!(in >> a >> b >> c >> d))
        continue;
        BB pos;
        if(!uci_parse_fen(a + " " + b + " " + c + " " + d + " 0 1", pos))
        continue;
        for(int ply=0; ply<40; ply++)
        {
            positions.push_back(pos);
            const int n = std::get<0>(all_moves(&pos, wfh));
            if(n==0)
            break;
            rng = rng*1103515245u + 12345u;
            pos = wfh[(rng>>16)%n];
        }
    }
    delete[] wfh;
    if(positions.empty())
    {
        expect(false, "tools/openings.epd not found (run from the repo root)");
        return;
    }
    int broken = 0, moved = 0;
    for(const BB& pos : positions)
    {
        BB copy = pos, m;
        uci_parse_fen(mirror_fen(copy.get_FEN()), m);
        broken += basic_eval(&pos, W) + basic_eval(&m, W) != 0;
        moved += basic_eval(&pos, W) != basic_eval(&pos, WEIGHTS_OG);
    }
    const std::string n = std::to_string(positions.size());
    expect(moved > (int)positions.size()/2, "the scrambled tables change the eval: " + std::to_string(moved) + "/" + n + " positions");
    expect(broken==0, "basic_eval() mirror-symmetric with scrambled tables: " + std::to_string(broken) + "/" + n + " broken");
}

int main()
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    const std::string file = read_file("weights/w1.txt");
    if(file.empty())
    {
        std::printf("weights/w1.txt not found (run from the repo root)\n");
        return 2;
    }
    round_trip(file);
    squares(file);
    broken(file);
    symmetry();
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
