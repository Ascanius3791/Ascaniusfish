// OWNERSHIP=Claude
#ifndef SYZYGY_POSITIONS_HPP
#define SYZYGY_POSITIONS_HPP
// Random legal positions of a given material, for the Syzygy reference file
// (tools/syzygy_reference.cpp) and the prober's test (diagnostics/syzygy_test.cpp).
// Include after lib/uci.hpp.
#include <dirent.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

// "KRPvKB" -> counts per Board[] index (P R N B Q K, white then black).
inline bool tb_material(const std::string& name, int counts[12])
{
    for(int i=0; i<12; i++) counts[i] = 0;
    int colour = 0;
    for(char c : name)
    {
        const char* types = "PRNBQK";
        const char* t = std::strchr(types, c);
        if(c == 'v') { if(colour) return false; colour = 1; }
        else if(t && c) counts[(t - types) + 6*colour]++;
        else return false;
    }
    return colour == 1 && counts[5] == 1 && counts[11] == 1;
}

// Table names (file stems) of the .rtbw files in dir, sorted.
inline std::vector<std::string> tb_table_names(const std::string& dir)
{
    std::vector<std::string> names;
    if(DIR* dp = opendir(dir.c_str()))
    {
        while(dirent* de = readdir(dp))
        {
            std::string f = de->d_name;
            if(f.size() > 5 && f.substr(f.size()-5) == ".rtbw")
            names.push_back(f.substr(0, f.size()-5));
        }
        closedir(dp);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// A random position with `counts` (swap_colours: white's pieces go to black and
// the other way round), `white_to_move`, and no check against the side that just
// moved. with_ep: the side that just moved has just pushed a pawn two squares
// next to a pawn of the side to move, and the FEN names the en passant square
// (the capture itself may still be illegal, e.g. pinned). Returns the FEN, or ""
// if with_ep is impossible for this material or tries ran out.
inline std::string tb_random_fen(const int counts_in[12], bool swap_colours, bool white_to_move, bool with_ep,
                                 std::mt19937_64& rng, int tries = 1000)
{
    int counts[12];
    for(int i=0; i<12; i++)
    counts[i] = counts_in[swap_colours ? (i+6) % 12 : i];
    const int us = white_to_move ? 0 : 6, them = white_to_move ? 6 : 0;
    if(with_ep && (!counts[us] || !counts[them]))
    return "";
    const char* letters = "PRNBQKprnbqk";
    std::uniform_int_distribution<int> any(0, 63);

    for(int attempt=0; attempt<tries; attempt++)
    {
        int board[64];
        for(int& b : board) b = -1;
        uint64_t reserved = 0;   // squares that must stay empty
        int left[12];
        for(int i=0; i<12; i++) left[i] = counts[i];
        int ep_square = -1;
        if(with_ep)
        {
            // Their pawn on its 4th rank, just arrived from two squares behind.
            int file = any(rng) % 8;
            int pawn_sq = white_to_move ? 32 + file : 24 + file;       // black on rank 5 / white on rank 4
            ep_square = white_to_move ? pawn_sq + 8 : pawn_sq - 8;
            int from_sq = white_to_move ? pawn_sq + 16 : pawn_sq - 16;
            int side = file == 0 ? 1 : file == 7 ? -1 : (any(rng) & 1 ? 1 : -1);
            board[pawn_sq] = them;
            board[pawn_sq + side] = us;
            left[them]--;
            left[us]--;
            reserved = 1ULL << ep_square | 1ULL << from_sq;
        }
        bool ok = true;
        for(int piece : {5, 11, 0, 6, 1, 7, 2, 8, 3, 9, 4, 10})
        for(int k=0; k<left[piece] && ok; k++)
        {
            int sq, guard = 0;
            do
            {
                sq = any(rng);
                if(++guard > 1000) { ok = false; break; }
            }
            while(board[sq] >= 0 || (reserved >> sq & 1) || ((piece == 0 || piece == 6) && (sq < 8 || sq >= 56)));
            if(ok) board[sq] = piece;
        }
        if(!ok) continue;

        std::string fen;
        for(int r=7; r>=0; r--)
        {
            int empty = 0;
            for(int f=0; f<8; f++)
            {
                int p = board[8*r + f];
                if(p < 0) { empty++; continue; }
                if(empty) fen += char('0' + empty);
                empty = 0;
                fen += letters[p];
            }
            if(empty) fen += char('0' + empty);
            if(r) fen += '/';
        }
        fen += white_to_move ? " w - " : " b - ";
        if(ep_square >= 0)
        {
            fen += char('a' + ep_square % 8);
            fen += char('1' + ep_square / 8);
        }
        else fen += "-";
        fen += " 0 1";

        BB pos;
        if(!uci_parse_fen(fen, pos))
        continue;
        // kings apart, and the side that just moved not in check
        int wk = __builtin_ctzll(pos.Board[5]), bk = __builtin_ctzll(pos.Board[11]);
        if(std::abs(wk % 8 - bk % 8) <= 1 && std::abs(wk / 8 - bk / 8) <= 1)
        continue;
        if(in_check(pos.Board, !white_to_move))
        continue;
        return fen;
    }
    return "";
}

#endif // SYZYGY_POSITIONS_HPP
