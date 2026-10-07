// OWNERSHIP=Claude
#ifndef UCI_HPP
#define UCI_HPP

#include "../ascaniusfish_2.hpp"
#include "search_control.hpp"
#include "move_ordering.hpp"
#include "time_manager.hpp"
#include "syzygy.hpp"
#include "gaviota.hpp"
#include "tb_search.hpp"
#include "nne.hpp"
#include "mate_search.hpp"
#include "engine_version.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <thread>

// UCI front end. Implements uci, isready, ucinewgame, position, go, stop,
// quit (setoption handles SyzygyPath, SyzygyProbeLimit, GaviotaTbPath, GaviotaTbCache, NNEFile, UseNNE, WeightsFile, MultiPV and UCI_AnalyseMode, debug/register/ponderhit are accepted and ignored), plus
// the non-standard "go perft N" for checking move generation from any FEN,
// the non-standard "hint depth D pv <moves>" (#79): a line found earlier for
// the position just set, seeded into the TT before the next "go" (see seed_hint()),
// the non-standard "route <moves>" (#100): another route to a position on
// the path, which an analysis search's path refresh refreshes too, and the
// correspondence mode's (#101) "ttmark" (an entry with the user's mark) and
// "ttdemote" (every unmarked entry becomes a move-ordering hint).
// The search runs on its own thread so "stop"/"isready" are answered while
// it thinks; see lib/search_control.hpp for how a search is aborted.

// The engine's TT, plus what an analysis needs (#100, docs/plans/tt_knowledge_reuse.md).
// Analyse walks the move tree in any order, so an entry stored before something
// below it was searched deeper or proven would cut before the search reaches that
// again. refresh_path() demotes such entries before an analysis search, and
// store_proven() keeps a verified root mate that insert() would drop under a
// deeper entry. Uses only lookup_table_base's protected members.
struct UCI_Table : lookup_table
{
    // The entry of a position, or nullptr.
    TT_slot* find_slot(uint64_t zobrist_hash)
    {
        TT_bucket& bucket = table[get_hash(zobrist_hash)];
        for(int i=0;i<bucket.fill_count();i++)
        if(bucket.holds(i, zobrist_hash))
        return &bucket.slot[i];
        return nullptr;
    }

    // Before an analysis search: what was stored before this search learns
    // something below it must not cut. Demotes the root's entry if it is exact,
    // and every ancestor's entry, exact or bound, that is i plies above the root
    // with i <= its depth. A bound never cuts at the root (full window) and is
    // what the parent's null-window search needs back; an entry shallower than
    // its distance never saw the root. A path that ends `offset` plies above
    // the root (another route to an ancestor, #100's "route") counts its
    // distances from there. Returns the entries demoted.
    int refresh_path(const std::vector<BB>& path, int offset = 0)
    {
        int demoted = 0;
        const int root = (int)path.size()-1;
        for(int j=0;j<=root;j++)
        {
            TT_slot* s = find_slot(path[j].zobrist_hash);
            const int i = root-j+offset;
            if(s && (i==0 ? s->pv_line.bound_type==0 : i<=s->pv_line.depth))
            demoted += demote(*s);
        }
        return demoted;
    }

    // A verified mate for the root holds at every depth; insert() would keep an
    // older, deeper bound over it. Written over that entry, at the deeper depth
    // of the two so it is not the first one evicted.
    void store_proven(uint64_t zobrist_hash, const PV_Line& pv)
    {
        TT_entry entry;
        entry.zobrist_hash = zobrist_hash;
        entry.initialized = true;
        entry.pv_line = pv;
        entry.pv_line.bound_type = 0;
        TT_slot* s = find_slot(zobrist_hash);
        if(!s)
        {
            insert(entry);
            return;
        }
        entry.pv_line.depth = std::max(entry.pv_line.depth, s->pv_line.depth);
        entry.search_id = current_search_id;
        set_tt_mark(entry.pv_line, tt_mark(s->pv_line));
        *s = TT_slot(entry);
    }

    // The correspondence mode (#101): the user's marks. A table that holds one
    // ranks its victims by tier (value_for_victim_index()); one that never did
    // is lookup_table, victim for victim.
    bool marks_held = false;

    void reset()
    {
        lookup_table::reset();
        marks_held = false;
    }

    // "Save to root": `entry` goes in with `mark`. Where the position has an
    // entry already, the result insert() would keep stays (the deeper, a proof
    // over a score) and the higher mark of the two is the entry's. False if the
    // bucket is full of entries worth more (higher marks, proofs).
    bool import_marked(TT_entry entry, int16_t mark)
    {
        if(mark>0)
        marks_held = true;
        TT_slot* s = find_slot(entry.zobrist_hash);
        if(!s)
        {
            set_tt_mark(entry.pv_line, mark);
            insert(entry);
            return find_slot(entry.zobrist_hash)!=nullptr;
        }
        const TT_Result& held = s->pv_line;
        const bool proof = tt_proven(entry.pv_line), held_proof = tt_proven(held);
        const bool take = (proof && !held_proof) || (entry.pv_line.depth>held.depth && (proof || !held_proof))
                       || (entry.pv_line.depth==held.depth && entry.pv_line.bound_type==0 && held.bound_type!=0);
        const int16_t keep = std::max(mark, tt_mark(held));
        if(take)
        {
            entry.search_id = current_search_id;
            *s = TT_slot(entry);
        }
        set_tt_mark(s->pv_line, keep);
        return true;
    }

    // "Return to root": every unmarked entry becomes a move-ordering hint
    // (demote()), the whole table over. Returns the entries demoted; `marked`
    // gets how many marked ones there are.
    int demote_unmarked(int& marked)
    {
        int demoted = 0;
        marked = 0;
        for(int i=0;i<size;i++)
        for(int j=0;j<table[i].fill_count();j++)
        {
            if(tt_mark(table[i].slot[j].pv_line)>0)
            marked++;
            else
            demoted += demote(table[i].slot[j]);
        }
        return demoted;
    }

    protected:
    // With marks held: a proof first, then a mark (the higher first), then
    // lookup_table's value (depth, less the age). Within a tier the deeper.
    float value_for_victim_index(const TT_slot& slot) const override
    {
        if(!marks_held)
        return lookup_table::value_for_victim_index(slot);
        const TT_Result& r = slot.pv_line;
        if(tt_proven(r))
        return 1.2e7f + r.depth;
        if(tt_mark(r)>0)
        return 4e6f + 128.0f*tt_mark(r) + r.depth;
        return lookup_table::value_for_victim_index(slot);
    }

    private:
    // A vacuous lower bound at depth 0 (eval INT_MIN), the move kept for the
    // ordering: it never cuts and gives way to anything the search stores, since
    // insert() keeps a position's deeper entry. A proof stays, and so do a book
    // entry and a marked one (#101). True if the entry was demoted.
    static bool demote(TT_slot& slot)
    {
        if(slot.is_from_opening_book || tt_mark(slot.pv_line)>0)
        return false;
        TT_Result& r = slot.pv_line;
        const bool vacuous = r.depth==0 && r.bound_type==-1 && r.eval==INT_MIN;
        if(tt_proven(r) || vacuous)
        return false;
        r.depth = 0;
        r.bound_type = -1;
        r.eval = INT_MIN;
        return true;
    }
};

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

// The least node budget the mate search gets to check a claimed mate (#78);
// otherwise as many nodes as the deepening has spent so far.
constexpr long long MATE_VERIFY_MIN_NODES = 100000;

// Parses a FEN (4-6 fields; missing clocks default to "0 1"). Returns false
// and leaves `out` untouched if the placement/side/castling/ep fields are malformed.
bool uci_parse_fen(const std::string& fen, BB& out);

// Finds the legal move `uci` (e.g. "e2e4", "e7e8q", "e1g1") in `pos`.
bool uci_apply_move(const BB& pos, const std::string& uci, BB& out);

// Legal move-path count to `depth` from `pos`; `buf` is BB scratch
// (depth*~256 entries suffice).
long long perft(const BB* pos, int depth, BB* buf);

// MultiPV (#69): one depth's `lines_wanted` lines (at most the legal moves),
// best first for the side to move, with distinct first moves. 1 = the plain
// minimax() call. Throws search_aborted like minimax().
std::vector<PV_Line> multipv_search(const BB& root, BB* wfh, int depth, lookup_table* table, BB* path_history, int ply,
                                    const CuckooCycleTable* cycle_table, int lines_wanted, const WEIGHTS& W = WEIGHTS_OG);

// "cp N" or "mate N", from the side to move's point of view.
std::string uci_score(int eval, bool white_to_move);

class UCI_Engine
{
    public:
    UCI_Engine();
    ~UCI_Engine();
    int loop();  // reads stdin until "quit" or EOF

    private:
    UCI_Table* table;
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
    bool tb_classes(const BB& root, const std::vector<BB>& children, std::vector<int>& cls, std::vector<int>& key);
    bool tb_root_filter(const BB& root, const std::vector<BB>& children, std::vector<int>& keep, int& tb_class);
    bool dtm_root(const BB& root, const std::vector<BB>& children, const UCI_Limits& limits, long long start_ns);
    // Checks the mate `pv` claims with the mate search (#78): 1 confirmed (pv
    // then holds the mate found: the quickest if full width ruled out every
    // shorter one; its line too if the root side mates and may_change_line or
    // the line starts with pv's move), -1 refuted, 0 out of budget or time.
    int verify_mate(const BB& root, PV_Line& pv, long long budget, bool may_change_line);
    void search_tb_root(const BB& root, const std::vector<Move>& moves, const std::vector<BB>& children, const std::vector<int>& keep, int tb_class, int ply, const UCI_Limits& limits, long long start_ns);
    std::string syzygy_dir;
    std::string gaviota_dir;                   // GaviotaTbPath (#77)
    int gaviota_cache_mb = 32;                 // GaviotaTbCache
    std::string nne_file = "nets/nne_d6.bin";  // NNEFile
    bool use_nne = true;                       // UseNNE; nne::enabled says whether it is in effect
    bool nne_applied = false;                  // apply_nne() has run; the default is applied on the first isready/go
    WEIGHTS weights = WEIGHTS_OG;              // WeightsFile (#85): the weight set every search uses, default the compiled-in one
    void apply_weights(const std::string& file);
    int multipv = 1;                           // MultiPV (#69): lines per depth, best first; 1 = the plain search
    bool narrow = false, narrow_deeper = false; // TTNarrowing, TTNarrowingDeeper (#65); the second only counts with the first
    void apply_nne();
    std::vector<std::string> pv_to_uci(const BB& root, const PV_Line& pv);
    // #82: a TT entry keeps only its best move, so a line ends where an exact TT
    // hit returned it. TTWalk extends each iteration's lines by following the
    // TT's moves from their end; ply is the root's index in path_history.
    bool tt_walk = true;
    void extend_pv_from_tt(const BB& root, PV_Line& pv, int ply);
    std::string nne_hash;                      // the loaded net's file hash, for the provenance line
    // "hint" (#79): the line and the depth it was searched to, for the position
    // of the last "position" command, which drops it again.
    int hint_depth = 0;
    std::vector<std::string> hint_pv;
    int seed_hint(const BB& root);
    // An analysis search refreshes its path (UCI_Table::refresh_path()): on
    // "go infinite", or any "go" with UCI_AnalyseMode on.
    bool analyse_mode = false;
    // "route <moves>" (#100): another route from the "position" command's start
    // to a position on its path, a transposition the path refresh refreshes
    // too. Dropped by the next "position", like the hint.
    std::vector<std::vector<std::string>> routes;
    int refresh_routes();
    void handle_ttmark(const std::vector<std::string>& tokens);
    void handle_ttdemote();
    std::string provenance() const;
};

int uci_loop();

#include "../src/uci.cpp"

#endif // UCI_HPP
