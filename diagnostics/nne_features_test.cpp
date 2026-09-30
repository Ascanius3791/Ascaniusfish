// OWNERSHIP=Claude
// lib/nne.hpp's inputs: a position and its mirror (ranks flipped, colours and
// the side to move swapped) are the same position for the mover, so they must
// give the same inputs; plus a few indices worked out by hand.
//   g++ -O2 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/nne_features_test diagnostics/nne_features_test.cpp
#include "../lib/uci.hpp"
#include "../lib/nne.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("  %-60s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    failures += !ok;
}

static std::vector<int> features(const std::string& fen)
{
    BB pos;
    if(!uci_parse_fen(fen, pos))
    return {-1};
    int out[nne::MAX_ACTIVE];
    int n = nne::active_features(pos, out);
    return std::vector<int>(out, out+n);
}

static std::string swap_case(std::string s)
{
    for(char& c : s)
    c = isupper((unsigned char)c) ? tolower(c) : toupper(c);
    return s;
}

// The same position seen from the other side.
static std::string mirror(const std::string& fen)
{
    std::istringstream in(fen);
    std::string board, side, castling, ep, half = "0", full = "1";
    in >> board >> side >> castling >> ep >> half >> full;
    std::vector<std::string> ranks;
    std::stringstream b(board);
    for(std::string r; std::getline(b, r, '/');)
    ranks.insert(ranks.begin(), swap_case(r));
    std::string flipped;
    for(size_t i=0;i<ranks.size();i++)
    flipped += (i ? "/" : "") + ranks[i];
    if(castling!="-")
    castling = swap_case(castling);
    if(ep!="-")
    ep[1] = ep[1]=='3' ? '6' : '3';
    return flipped + " " + (side=="w" ? "b" : "w") + " " + castling + " " + ep + " " + half + " " + full;
}

static bool has(const std::vector<int>& f, int i)
{
    for(int x : f)
    if(x==i)
    return true;
    return false;
}

int main()
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::printf("lib/nne.hpp: a position and its mirror give the same inputs\n");
    const char* fens[] =
    {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
        "r2qkb1r/1bpn1pp1/pp1p2np/4p3/3PP3/2PB1N1P/PP1N1PPB/R2Q1RK1 w kq - 0 11",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
    };
    for(const char* fen : fens)
    {
        std::vector<int> a = features(fen), b = features(mirror(fen));
        check(a==b && a[0]>=0, std::string(fen).substr(0, 58));
    }

    std::printf("indices by hand\n");
    std::vector<int> start = features(fens[0]);
    check(start.size()==32+4, "start: 32 pieces and 4 castling rights");
    check(has(start, 0*64+12) && has(start, 5*64+4), "own e2 pawn (12) and own e1 king (5*64+4)");
    check(has(start, (6+0)*64+52) && has(start, (6+5)*64+60), "opponent e7 pawn and e8 king");
    check(has(start, 768) && has(start, 771), "castling inputs 768..771");
    std::vector<int> after_e4 = features(fens[1]);
    check(has(after_e4, (6+0)*64+(28^56)), "black to move: white's e4 pawn is the opponent's, on e5");
    check(!has(after_e4, 772+4), "e3 is no en passant input: no black pawn can take");
    std::vector<int> ep = features(fens[2]);
    check(has(ep, 772+5), "e5 pawn next to f5: the f-file input is on");
    std::vector<int> no_castle = features(fens[5]);
    check(!has(no_castle, 768) && !has(no_castle, 769) && !has(no_castle, 770) && !has(no_castle, 771), "no rights, no castling inputs");

    std::printf("\n%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
