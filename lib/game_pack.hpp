// OWNERSHIP=Claude
#ifndef GAME_PACK_HPP
#define GAME_PACK_HPP
#include "uci.hpp"
#include <cstdint>
#include <string>
#include <vector>

// The tuner's data as compact binary (#116): games instead of FEN lines, so the
// whole set (1.08 M games, every split and chain position) fits in a release asset
// and the tools read it without PGN or FEN parsing.
//
// Packed_Pos: one position in 32 bytes, every BB field a FEN sets (boards, side,
// castling, en passant, halfmove clock, fullmove number). unpack_position() builds
// the BB uci_parse_fen() builds from that FEN: Zobrist hash computed, no
// repetitions, empty lazy cache. The tuner keeps its positions in this form.
//
// A game pack file (little endian):
//   header   char magic[8] "AFGPACK\0", u32 version (1), u32 tag bytes,
//            u64 games, u64 marks per set [4], then the tag (text: what the data is)
//   games    back to back, each:
//            u32 game        its number in the source archive
//            u16 white_elo, u16 black_elo
//            u8  result      0 = 0-1, 1 = draw, 2 = 1-0
//            u8  0           reserved
//            u16 plies, u16 marks
//            u8  move[plies] the rank of the move played among the position's legal
//                            moves sorted by (from, to, promotion), move_key()
//            marks, 7 bytes each, by ply: u8 set, u16 ply, u32 index
// Every game starts from the standard start position. A mark says the position
// after `ply` plies belongs to a set: PACK_TRAIN, PACK_VALID, PACK_TEST (index: its
// line in that split's file, so a fit sums in the same order) or PACK_CHAIN
// (tools/tune's bootstrap positions, index 0). A position may carry several marks.
// The move ranks depend only on the rules, not on the move generator's order.

struct Packed_Pos
{
    uint64_t occupancy;
    uint8_t pieces[16];     // 4 bits per occupied square, lowest square first: the Board index 0-11
    uint8_t flags;          // bit 0 white to move; bits 1-4 castle[1][1], castle[1][0], castle[0][1], castle[0][0]
    uint8_t en_passant;     // its square, 64 = none
    uint16_t halfmoves;
    uint16_t fullmove;
    uint8_t reserved[2];
};
static_assert(sizeof(Packed_Pos)==32, "Packed_Pos is 32 bytes");

// False if the position does not fit (more than 32 pieces, a clock beyond 65535).
bool pack_position(const BB& pos, Packed_Pos& out);
void unpack_position(const Packed_Pos& in, BB& out);
bool operator==(const Packed_Pos& a, const Packed_Pos& b);

enum Pack_Set { PACK_TRAIN = 0, PACK_VALID = 1, PACK_TEST = 2, PACK_CHAIN = 3, PACK_SETS = 4 };
const char* const PACK_SET_NAMES[PACK_SETS] = { "train", "valid", "test", "chain" };
constexpr int PACK_MARK_BYTES = 7;

struct Pack_Mark { int set, ply; uint32_t index; };

struct Pack_Game
{
    uint32_t number = 0;
    int white_elo = 0, black_elo = 0, result = 0;   // result: 0 = 0-1, 1 = draw, 2 = 1-0
    int plies = 0, n_marks = 0;
    const uint8_t* moves = nullptr;
    const uint8_t* marks = nullptr;
    Pack_Mark mark(int k) const;
};

// The game whose record starts at p (in a file, or as append_game() made it).
Pack_Game read_game(const uint8_t* p);

// A pack file, mapped read-only. open() checks the header and every record's bounds.
class Game_Pack
{
    public:
    Game_Pack() = default;
    Game_Pack(const Game_Pack&) = delete;
    Game_Pack& operator=(const Game_Pack&) = delete;
    ~Game_Pack();
    bool open(const std::string& path, std::string& error);
    size_t size() const { return at.size(); }
    Pack_Game game(size_t i) const;
    uint64_t marks(int set) const { return n_marks[set]; }
    const std::string& tag() const { return tag_text; }

    private:
    const uint8_t* data = nullptr;
    size_t bytes = 0;
    std::vector<size_t> at;      // each game's record
    uint64_t n_marks[PACK_SETS] = {0, 0, 0, 0};
    std::string tag_text;
};

// The order the ranks count in: from, to, promotion.
inline int move_key(const Move& m) { return m.from<<9 | m.to<<3 | (m.promotion_piece_type+1); }

// The rank of the move pos -> child (same boards and side) among pos's legal moves, or -1.
int move_rank(const BB& pos, const BB& child);
// The position after the legal move of that rank; false if there are not that many.
bool play_rank(const BB& pos, int rank, BB& child);

// Replays g from the start up to its last mark of a set in `sets` (a bit mask,
// 1<<PACK_TRAIN | ...), calling f(const Pack_Mark&, const BB& pos) for every such mark
// in ply order, with pos as uci_parse_fen() builds it from the position's FEN (the en
// passant square only when that capture is legal, the game's clocks). False, with
// `error`, if a rank is out of range.
template<class F>
bool replay(const Pack_Game& g, unsigned sets, F&& f, std::string& error);

// Appends one game record to `out`. Marks are sorted by ply here.
void append_game(std::string& out, uint32_t number, int white_elo, int black_elo, int result,
                 const std::vector<uint8_t>& ranks, std::vector<Pack_Mark> marks);

// Writes a pack: open(), then records as append_game() made them, then close(),
// which puts the counts into the header.
class Pack_Writer
{
    public:
    bool open(const std::string& path, const std::string& tag, std::string& error);
    bool add(const std::string& records, size_t games, const uint64_t marks[PACK_SETS]);
    bool close(std::string& error);

    private:
    FILE* file = nullptr;
    std::string path;
    bool ok = true;
    uint64_t games = 0, n_marks[PACK_SETS] = {0, 0, 0, 0};
};

#include "../src/game_pack.cpp"

#endif // GAME_PACK_HPP
