// OWNERSHIP=Claude
// Game-level chess rules for tools that play or record whole games
// (tools/match.cpp, tools/make_openings.cpp): SAN, FEN, repetition keys and
// game-end detection. Move legality comes from the engine's own all_moves().
//
// The engine's BB clocks are not usable for game rules: the halfmove clock
// is never reset on captures/pawn moves and BB::move counts plies. Game
// keeps its own clocks.
#ifndef TOOLS_GAME_RULES_HPP
#define TOOLS_GAME_RULES_HPP
#include "../lib/uci.hpp"
#include <array>
#include <string>
#include <vector>

constexpr int MAX_LEGAL_MOVES = 256;

inline int piece_on(const BB& pos, int square)  // 0-11, or -1 if empty
{
    for(int i=0;i<12;i++)
    if(pos.Board[i] & (1ULL<<square))
    return i;
    return -1;
}

inline int square_of(const std::string& uci, int at)
{
    return (uci[at]-'a') + 8*(uci[at+1]-'1');
}

// En passant square only if an en passant capture is actually legal (as
// FEN and the repetition rule treat it).
inline uint64_t effective_en_passant(const BB& pos, const BB* children, int n)
{
    if(!pos.en_passant)
    return 0;
    uint64_t own_pawns = pos.Board[pos.white_move ? 0 : 6];
    for(int i=0;i<n;i++)
    if((children[i].Board[pos.white_move ? 0 : 6] & ~own_pawns) & pos.en_passant)
    return pos.en_passant;
    return 0;
}

// SAN of the move pos -> children[k], with +/# suffix.
inline std::string san(const BB& pos, const BB* children, int n, int k)
{
    std::string uci = get_UCI(&pos, children+k);
    int from = square_of(uci, 0), to = square_of(uci, 2);
    int piece = piece_on(pos, from) % 6;  // 0 P, 1 R, 2 N, 3 B, 4 Q, 5 K
    std::string s;
    if(piece==5 && (to-from==2 || from-to==2))
    s = to>from ? "O-O" : "O-O-O";
    else
    {
        bool capture = piece_on(pos, to)>=0 || (piece==0 && (from-to)%8!=0);
        if(piece==0)
        {
            if(capture) s += char('a'+from%8);
        }
        else
        {
            s += "PRNBQK"[piece];
            bool clash = false, same_file = false, same_rank = false;
            for(int i=0;i<n;i++)
            {
                if(i==k) continue;
                std::string other = get_UCI(&pos, children+i);
                int of = square_of(other, 0);
                if(square_of(other, 2)!=to || of==from || piece_on(pos, of)%6!=piece)
                continue;
                clash = true;
                same_file |= of%8==from%8;
                same_rank |= of/8==from/8;
            }
            if(clash && (!same_file || same_rank)) s += char('a'+from%8);
            if(clash && same_file) s += char('1'+from/8);
        }
        if(capture) s += 'x';
        s += uci.substr(2, 2);
        if(uci.size()==5)
        {
            s += '=';
            s += (char)toupper(uci[4]);
        }
    }
    const BB& child = children[k];
    if(child.get_in_check())
    {
        BB grandchildren[MAX_LEGAL_MOVES];
        s += std::get<0>(all_moves(&child, grandchildren))==0 ? '#' : '+';
    }
    return s;
}

// Everything the repetition rule compares: pieces, side to move, castling
// rights and a legal en passant capture.
using Position_Key = std::array<uint64_t, 14>;

inline Position_Key position_key(const BB& pos, uint64_t en_passant)
{
    Position_Key key;
    for(int i=0;i<12;i++)
    key[i] = pos.Board[i];
    key[12] = pos.white_move | pos.castle[1][1]<<1 | pos.castle[1][0]<<2 | pos.castle[0][1]<<3 | pos.castle[0][0]<<4;
    key[13] = en_passant;
    return key;
}

inline bool insufficient_material(const BB& pos)
{
    const uint64_t* b = pos.Board;
    if(b[0]|b[1]|b[4]|b[6]|b[7]|b[10])
    return false;
    uint64_t knights = b[2]|b[8], bishops = b[3]|b[9];
    if(__builtin_popcountll(knights|bishops)<=1)
    return true;
    const uint64_t DARK = 0x55AA55AA55AA55AAULL;
    return !knights && (!(bishops & DARK) || !(bishops & ~DARK));
}

enum class Outcome { ONGOING, WHITE_WINS, BLACK_WINS, DRAW };

// A game from a start position: positions, clocks and the end-of-game rules.
struct Game
{
    std::vector<BB> positions;          // start position, then after every move
    std::vector<std::string> uci_moves;
    std::vector<std::string> san_moves;
    std::vector<Position_Key> keys;     // one per position
    int halfmove_clock = 0;             // for the 50-move rule
    int start_fullmove = 1;
    BB children[MAX_LEGAL_MOVES];       // legal moves of positions.back()
    int n_children = 0;

    void start(const BB& pos, int halfmoves, int fullmove)
    {
        positions.assign(1, pos);
        uci_moves.clear();
        san_moves.clear();
        halfmove_clock = halfmoves;
        start_fullmove = fullmove;
        n_children = std::get<0>(all_moves(&pos, children));
        keys.assign(1, position_key(pos, effective_en_passant(pos, children, n_children)));
    }

    // Applies a UCI move; false if it isn't legal here.
    bool play(const std::string& uci)
    {
        for(int k=0;k<n_children;k++)
        {
            if(get_UCI(&positions.back(), children+k)!=uci)
            continue;
            const BB& pos = positions.back();
            BB next = children[k];
            san_moves.push_back(san(pos, children, n_children, k));
            uci_moves.push_back(uci);
            bool pawn_move = pos.Board[pos.white_move ? 0 : 6] != next.Board[pos.white_move ? 0 : 6];
            uint64_t before = 0, after = 0;
            for(int i=0;i<12;i++) { before |= pos.Board[i]; after |= next.Board[i]; }
            bool capture = __builtin_popcountll(after) < __builtin_popcountll(before);
            halfmove_clock = pawn_move || capture ? 0 : halfmove_clock+1;
            positions.push_back(next);
            n_children = std::get<0>(all_moves(&next, children));
            keys.push_back(position_key(next, effective_en_passant(next, children, n_children)));
            return true;
        }
        return false;
    }

    int fullmove() const
    {
        return start_fullmove + ((int)uci_moves.size() + !positions[0].white_move)/2;
    }

    // Standard FEN of the current position with the tracked clocks.
    std::string fen() const
    {
        return fen_of(positions.back(), keys.back()[13], halfmove_clock, fullmove());
    }

    static std::string fen_of(const BB& pos, uint64_t en_passant, int halfmoves, int fullmove)
    {
        BB copy = pos;
        copy.en_passant = en_passant;
        std::string f = get_FEN(copy);  // its clock fields are the engine's; replace them
        f = f.substr(0, f.rfind(' '));
        f = f.substr(0, f.rfind(' '));
        return f + " " + std::to_string(halfmoves) + " " + std::to_string(fullmove);
    }

    // Checks the rules in order mate/stalemate, 50 moves, repetition,
    // material; `reason` gets a PGN-style description.
    Outcome outcome(std::string& reason) const
    {
        const BB& pos = positions.back();
        if(n_children==0)
        {
            if(pos.get_in_check())
            {
                reason = pos.white_move ? "black mates" : "white mates";
                return pos.white_move ? Outcome::BLACK_WINS : Outcome::WHITE_WINS;
            }
            reason = "stalemate";
            return Outcome::DRAW;
        }
        if(halfmove_clock>=100)
        {
            reason = "fifty move rule";
            return Outcome::DRAW;
        }
        int repeats = 0;
        for(int i=(int)keys.size()-1; i>=0 && i>=(int)keys.size()-1-halfmove_clock; i-=2)
        repeats += keys[i]==keys.back();
        if(repeats>=3)
        {
            reason = "3-fold repetition";
            return Outcome::DRAW;
        }
        if(insufficient_material(pos))
        {
            reason = "insufficient material";
            return Outcome::DRAW;
        }
        return Outcome::ONGOING;
    }
};

#endif // TOOLS_GAME_RULES_HPP
