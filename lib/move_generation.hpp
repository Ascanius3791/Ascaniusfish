// OWNERSHIP=Ascanius
#ifndef MOVE_GENERATION_HPP
#define MOVE_GENERATION_HPP
#include "../src/checks.cpp"
#include "../src/zobrist.cpp"
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

struct PV_Line
{
    Move moves[MAX_PV_Lenght];//the depth doesnt have to match with the number of filled in moves, because upwards captures operate at increased depth
    int depth = 0;//search depth at which the PV was found
    int eval = 0;
    int bound_type = 2; // 0 = exact, -1 = lower bound, 1 = upper bound, 2= not set
    int current_lenght = 0;//lenght of the PV line(>=depth, because 1 upwards captures -> depth+1)
    public:
    void append(const Move move, int eval);
    PV_Line(const Move move,int depth, const PV_Line* const pv_line=0);
    PV_Line();
    PV_Line(int eval);
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
