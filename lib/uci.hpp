// OWNERSHIP=Claude
#ifndef UCI_HPP
#define UCI_HPP

#include "../ascaniusfish_2.hpp"
#include "search_control.hpp"
#include "move_ordering.hpp"
#include "time_manager.hpp"
#include "syzygy.hpp"
#include "tb_search.hpp"
#include "nne.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <thread>

// UCI front end. Implements uci, isready, ucinewgame, position, go, stop,
// quit (setoption handles SyzygyPath, SyzygyProbeLimit, NNEFile and UseNNE, debug/register/ponderhit are accepted and ignored), plus
// the non-standard "go perft N" for checking move generation from any FEN.
// The search runs on its own thread so "stop"/"isready" are answered while
// it thinks; see lib/search_control.hpp for how a search is aborted.

struct UCI_Limits
{
    int depth = 0;              // 0 = no depth limit
    long long movetime_ms = 0;  // 0 = none
    long long wtime = -1, btime = -1, winc = 0, binc = 0;
    int movestogo = 0;
    bool infinite = false;      // bestmove only after "stop"
    int perft = 0;              // >0: run perft instead of a search
};

constexpr int UCI_MAX_DEPTH = 64;
constexpr int UCI_WFH_SIZE = 1 << 16;  // BB scratch for the whole search tree

// Parses a FEN (4-6 fields; missing clocks default to "0 1"). Returns false
// and leaves `out` untouched if the placement/side/castling/ep fields are malformed.
bool uci_parse_fen(const std::string& fen, BB& out);

// Finds the legal move `uci` (e.g. "e2e4", "e7e8q", "e1g1") in `pos`.
bool uci_apply_move(const BB& pos, const std::string& uci, BB& out);

// Legal move-path count to `depth` from `pos`; `buf` is BB scratch
// (depth*~256 entries suffice).
long long perft(const BB* pos, int depth, BB* buf);

// "cp N" or "mate N", from the side to move's point of view.
std::string uci_score(int eval, bool white_to_move);

class UCI_Engine
{
    public:
    UCI_Engine();
    ~UCI_Engine();
    int loop();  // reads stdin until "quit" or EOF

    private:
    lookup_table* table;
    BB* wfh;
    BB* path_history;   // [MAX_SEARCH_PLY], repetition context for minimax()
    BB* pv_buf;         // scratch for walking the PV / applying moves
    CuckooCycleTable* cycle_table;
    TimeManager* tm;       // set by "go" on a clock (without movetime), else nullptr
    Lambda_History lambda_history;  // persists across moves; reset on ucinewgame
    std::vector<BB> game;  // root FEN position followed by every position after "moves"

    std::thread search_thread;
    std::mutex out_mutex;

    void send(const std::string& line);
    void ensure_table();
    void stop_search();
    void handle_position(const std::vector<std::string>& tokens);
    void handle_go(const std::vector<std::string>& tokens);
    void search(UCI_Limits limits, long long start_ns);
    void perft_divide(int depth);
    bool tb_root_filter(const BB& root, const std::vector<BB>& children, std::vector<int>& keep, int& tb_class);
    void search_tb_root(const BB& root, const std::vector<Move>& moves, const std::vector<BB>& children, const std::vector<int>& keep, int tb_class, int ply, const UCI_Limits& limits, long long start_ns);
    std::string syzygy_dir;
    std::string nne_file = "nets/nne_d6.bin";  // NNEFile
    bool use_nne = true;                       // UseNNE; nne::enabled says whether it is in effect
    bool nne_applied = false;                  // apply_nne() has run; the default is applied on the first isready/go
    void apply_nne();
    std::vector<std::string> pv_to_uci(const BB& root, const PV_Line& pv);
};

int uci_loop();

#include "../src/uci.cpp"

#endif // UCI_HPP
