// OWNERSHIP=Claude
// What the analysis has already found in this game, kept per position.
//
// Moving the cursor ends the search that was running, so without this every
// step back into a game you have just analysed shows an empty bar and
// "Starting the search…" for as long as the engine needs to get going again —
// even though the answer was on the screen a moment ago. Keeping the best
// result per position means arriving anywhere already looked at fills the bar,
// the score and the line at once, and the live search only has to improve on it.
//
// Keyed by the position (tools/game_rules.hpp's repetition key), not by a tree
// node: the same position reached through a side line, or by a transposition,
// is the same analysis. That is also what makes the entries safe — an entry is
// only ever handed back for the position it was made in, so nothing stale can
// be shown.
//
// Memory only, and a fixed number of slots: this is a convenience on top of the
// engine's own table, not a second transposition table.
#ifndef GUI_ANALYSIS_STORE_HPP
#define GUI_ANALYSIS_STORE_HPP
#include "../tools/game_rules.hpp"
#include "../tools/uci_engine.hpp"

constexpr int ANALYSIS_STORE_SIZE = 1024;   // slots; a power of two, used as a mask
constexpr int ANALYSIS_STORE_PV = 32;       // moves of a line worth keeping

struct Analysis_Entry
{
    Position_Key key{};
    Search_Info info;
    bool used = false;
};

class Analysis_Store
{
    public:
    // A new game throws all of it away: the entries belong to the game they
    // were made in and say nothing about the next one.
    void clear()
    {
        for(int i=0;i<ANALYSIS_STORE_SIZE;i++)
        slots[i].used = false;
    }

    // Keeps this result for `key`. For the same position the kept one survives
    // only if it is at least as deep and has at least as many lines (MultiPV,
    // #69) — see covers(); a different position simply takes the slot, since
    // the one being looked at now is the one worth having.
    void put(const Position_Key& key, const Search_Info& info)
    {
        Analysis_Entry& slot = slots[index(key)];
        if(slot.used && slot.key==key && covers(slot.info, info))
        return;
        slot.key = key;
        slot.info = info;
        if(slot.info.pv.size()>ANALYSIS_STORE_PV)
        slot.info.pv.resize(ANALYSIS_STORE_PV);
        for(Search_Line& more : slot.info.more)
        if(more.pv.size()>ANALYSIS_STORE_PV)
        more.pv.resize(ANALYSIS_STORE_PV);
        slot.used = true;
    }

    // Whether `kept` tells at least what `fresh` does: as deep, as many lines.
    // A deep single line does not stand in for the K lines asked for now.
    static bool covers(const Search_Info& kept, const Search_Info& fresh)
    {
        return kept.depth>=fresh.depth && kept.more.size()>=fresh.more.size();
    }

    // The best result kept for `key`, if it is still the one in its slot.
    bool get(const Position_Key& key, Search_Info& out) const
    {
        const Analysis_Entry& slot = slots[index(key)];
        if(!slot.used || !(slot.key==key))
        return false;
        out = slot.info;
        return true;
    }

    private:
    Analysis_Entry slots[ANALYSIS_STORE_SIZE];

    // The key's 14 words folded into one slot number. Not a Zobrist hash — the
    // key is the whole position, so a wrong slot only ever means a miss or a
    // replacement, never a wrong answer: get() compares the key itself.
    static int index(const Position_Key& key)
    {
        uint64_t h = 0xcbf29ce484222325ULL;
        for(uint64_t word : key)
        h = (h ^ word) * 0x100000001b3ULL;
        return (int)((h ^ h>>32) & (ANALYSIS_STORE_SIZE-1));
    }
};

#endif // GUI_ANALYSIS_STORE_HPP
