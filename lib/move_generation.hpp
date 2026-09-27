// OWNERSHIP=Ascanius
#ifndef MOVE_GENERATION_HPP
#define MOVE_GENERATION_HPP
#include "../src/checks.cpp"
#include "../src/zobrist.cpp"
#include "pv_pool.hpp"
#include <tuple>
#include <vector>

struct Move
{
    int from;
    int to;
    int promotion_piece_type;
    bool is_castling;
    bool is_en_passant;
    Move();
    Move(int from, int to, int promotion_piece_type=-1, bool is_castling=false, bool is_en_passant=false);   
    // define == operator for Move struct
    bool operator==(const Move& other) const;
};

// Moves held inline in a PV_Line before it needs its first extension chunk.
// PV_Line used to carry a flat Move[MAX_PV_Lenght] (128 moves, 2048 bytes) and
// was ~1% full: measured mean PV length is 1.4 moves from startpos at depth 5
// and 2.3 in a KPvK endgame at depth 9, with 95.6% of live transposition-table
// entries holding 2 moves or fewer (diagnostics/pv_length_histogram.cpp). Since
// TT_entry embeds a PV_Line, that array was most of the transposition table.
// 8 inline moves cover 99.7% of entries with no chunk at all.
// Override with -DPV_CHUNK_SIZE=n to retune, or to compare against the old flat
// layout: PV_CHUNK_SIZE==MAX_PV_Lenght never allocates a chunk and makes PV_Line
// behave exactly like the Move[MAX_PV_Lenght] array it replaced.
#ifndef PV_CHUNK_SIZE
#define PV_CHUNK_SIZE 8
#endif
constexpr int PV_CHUNK = PV_CHUNK_SIZE;

// The rest of a PV line, in PV_CHUNK-move links. `next` is what Chunk_pool
// threads its freelist through, so it must stay the chunk's own member.
struct PV_extension
{
    Move moves[PV_CHUNK];
    PV_extension* next = nullptr;
};

// The one pool every PV_Line takes its extension chunks from. A function-local
// static, not a global object, so it cannot be used before it is constructed
// whatever order the translation unit's other statics run in.
Chunk_pool<PV_extension, PV_POOL_CAP>& pv_extension_pool();

// A principal variation: PV_CHUNK moves inline, and a chain of extension chunks
// that stays unallocated (extension==0) until a line actually outgrows the
// inline part - which the measurements above put at 0.33% of entries. Owns its
// chain, so it has full copy/move/destroy semantics; copying one clones the
// chain, and every copy is a deep copy, which the transposition table relies on
// (an entry must not alias the searching node's line).
//
// current_lenght is always the number of moves actually stored and readable, so
// existing `for(k=0;k<current_lenght;k++)` consumers stay correct even when the
// pool ran dry mid-line; `truncated` then records that the line is incomplete.
// Index with at()/set_move() rather than moves[] for any index that can reach
// PV_CHUNK - moves[] alone only covers the inline part.
struct PV_Line
{
    Move moves[PV_CHUNK];//the depth doesnt have to match with the number of filled in moves, because upwards captures operate at increased depth
    PV_extension* extension = nullptr;//0 until the line outgrows the inline moves
    int depth = 0;//search depth at which the PV was found
    int eval = 0;
    int bound_type = 2; // 0 = exact, -1 = lower bound, 1 = upper bound, 2= not set
    int current_lenght = 0;//lenght of the PV line(>=depth, because 1 upwards captures -> depth+1)
    bool truncated = false;//the extension pool ran dry: moves past current_lenght were dropped
    public:
    void append(const Move move, int eval);
    PV_Line(const Move move,int depth, const PV_Line* const pv_line=0);
    PV_Line();
    PV_Line(int eval);

    PV_Line(const PV_Line& other);
    PV_Line(PV_Line&& other) noexcept;
    PV_Line& operator=(const PV_Line& other);
    PV_Line& operator=(PV_Line&& other) noexcept;
    ~PV_Line();

    const Move& at(int i) const;//i>=current_lenght yields a default Move, never out-of-bounds
    bool set_move(int i, const Move& move);//grows the chain as needed; false if the pool is empty
    std::vector<Move> first_n(int n) const;//the first min(n,current_lenght) moves, flattened
    void clear_extension();//hands the chain back to the pool and drops it

    private:
    void clone_moves_from(const PV_Line& other);//deep-copies inline moves + chain
};



// Legal move generator. Computes checkers/pinned-piece/king-danger bitboards once
// per node instead of testing every pseudo-legal candidate by making it and calling
// in_check() (see TODO.md item 1) - verified move-for-move against all_moves_old()
// across ~17k nodes of standard perft test positions, see
// diagnostics/legal_movegen_perft_compare.cpp. En passant is still verified the
// make-then-in_check() way, since it's the one move type where removing two pawns
// from the same rank can expose a check a simple pin mask doesn't cover cheaply, and
// it's rare enough (<=2 per position) not to matter.
std::tuple<int,std::vector<Move>> all_moves(const BB* const original, BB* const wfh , int len_wfh=INT_MAX);// returns number of moves and writes them to wfh(write from here)

// The previous implementation - kept as a reference/fallback and as the trusted
// baseline in diagnostics/legal_movegen_perft_compare.cpp. Tests every pseudo-legal
// move by making it and calling in_check(); see TODO.md item 1.
std::tuple<int,std::vector<Move>> all_moves_old(const BB* const original, BB* const wfh , int len_wfh=INT_MAX);

std::string get_move(const BB* const original, const BB* const goal);

std::string moves_to_PGN(const BB& initial_position, const std::vector<Move>& moves);

bool one_move(const BB* const original);//returns true if there are legal moves


#endif // MOVE_GENERATION_HPP
