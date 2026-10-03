// OWNERSHIP=Claude
// The PTT, the persistent transposition table (#79): every long search of a
// position that was on the board, kept on disk like Lichess's cloud eval.
//
// gui/analysis_store.hpp keeps what this game's analyses found, in memory; this
// is its sibling that outlives the server. Coming back to a position — in any
// session, after a restart, from another worktree's GUI — shows its eval, depth
// and line at once with no engine running, and the stored line seeds the next
// search there (the engine's "hint" command, src/uci.cpp).
//
// One text file, one entry per line, tab-separated key=value fields:
//   fen=<FEN with clocks>  depth=  score=cp 35  nodes=  time=  pv=e2e4 e7e5 …
//   eval=<EVAL_VERSION>  nne=<net hash|off>  syzygy=  gaviota=  commit=  at=<unix s>
// Lines are only appended, under flock, so a second GUI can share the file; a
// later line for the same position replaces an earlier one when it is better
// (better()), and the file is rewritten without the replaced lines when they
// outnumber the live ones. Keyed like the analysis store, by the whole
// position (Position_Key, from the FEN), never by a hash.
//
// Only the server writes it, and only from searches of >=PTT_MIN_MS at
// MultiPV 1 of positions on the board (Session::ptt_offer()).
#ifndef GUI_PTT_HPP
#define GUI_PTT_HPP
#include "../tools/game_rules.hpp"
#include "../tools/uci_engine.hpp"
#include <cerrno>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <vector>

constexpr long long PTT_MIN_MS = 5000;   // a search shorter than this is not kept
constexpr int PTT_PV = 64;               // moves of a line worth keeping
// Whether a current entry's line is sent to the engine as a "hint" before it
// searches the position again (the "+" in Analyse, a Play or Watch move).
// Measured with tools/ptt_seed (docs/measurements/ptt_seed_2026-10-03.md).
constexpr bool PTT_SEED = true;

struct PTT_Entry
{
    std::string fen;           // the board, clocks included
    Search_Info info;          // depth, score (the mover's view), nodes, time, line; prov
    long long stored_at = 0;   // unix seconds
};

// The eval a session's searches are made with now: an entry is "current" only
// if it was found with the same one. eval_version 0 = unknown, nothing is.
struct PTT_Eval
{
    int eval_version = 0;
    std::string nne = "off";

    bool matches(const Search_Provenance& p) const
    {
        return eval_version>0 && p.eval_version==eval_version && p.nne==nne;
    }
};

class PTT
{
    public:
    std::string path;   // empty = off

    // Opens (and creates) the file and reads it. False, with `error`, if it
    // cannot be created; the PTT is then off.
    bool open(const std::string& file, std::string& error)
    {
        entries.clear();
        read_offset = 0;
        path.clear();
        if(file.empty())
        return true;
        const size_t slash = file.rfind('/');
        if(slash!=std::string::npos && slash>0)
        mkdir(file.substr(0, slash).c_str(), 0755);   // ~/.ascaniusfish; one level is all it needs
        const int fd = ::open(file.c_str(), O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC, 0644);
        if(fd<0)
        {
            error = "cannot open " + file + ": " + std::strerror(errno);
            return false;
        }
        ::close(fd);
        path = file;
        refresh();
        if(superseded>(long long)entries.size()+100)
        compact();
        return true;
    }

    bool on() const { return !path.empty(); }
    size_t size() const { return entries.size(); }

    // The entry for `key`, after reading whatever another GUI appended since.
    const PTT_Entry* get(const Position_Key& key)
    {
        if(!on())
        return nullptr;
        refresh();
        auto it = entries.find(key);
        return it==entries.end() ? nullptr : &it->second;
    }

    // Keeps `entry` for `key` if it beats what is there, and appends it to the
    // file. True if it was kept.
    bool put(const Position_Key& key, PTT_Entry entry)
    {
        if(!on())
        return false;
        refresh();
        if(entry.info.pv.size()>PTT_PV)
        entry.info.pv.resize(PTT_PV);
        entry.info.more.clear();
        if(!entry.stored_at)
        entry.stored_at = (long long)std::time(nullptr);
        auto it = entries.find(key);
        if(it!=entries.end() && !better(entry, it->second))
        return false;
        const std::string line = format(entry);
        const int fd = open_locked(O_WRONLY|O_CREAT|O_APPEND);
        if(fd<0)
        return false;
        const bool written = write(fd, line.data(), line.size())==(ssize_t)line.size();
        flock(fd, LOCK_UN);
        ::close(fd);
        if(!written)
        return false;
        if(it!=entries.end())
        superseded++;
        entries[key] = entry;
        return true;
    }

    // Whether `fresh` should replace `kept` for the same position: a newer
    // eval version wins over an older one and never the other way round; within
    // one version and net the deeper search wins (an equal one does not); a
    // search with another net of the same version is the newer opinion.
    static bool better(const PTT_Entry& fresh, const PTT_Entry& kept)
    {
        const Search_Provenance &f = fresh.info.prov, &k = kept.info.prov;
        if(f.eval_version!=k.eval_version)
        return f.eval_version>k.eval_version;
        if(f.nne!=k.nne)
        return true;
        return fresh.info.depth>kept.info.depth;
    }

    static std::string format(const PTT_Entry& e)
    {
        const Search_Info& s = e.info;
        std::string pv;
        for(const std::string& m : s.pv)
        pv += (pv.empty() ? "" : " ") + m;
        return "fen=" + e.fen + "\tdepth=" + std::to_string(s.depth)
             + "\tscore=" + s.score_kind + " " + s.score_value
             + "\tnodes=" + std::to_string(s.nodes) + "\ttime=" + std::to_string(s.time_ms)
             + "\tpv=" + pv
             + "\teval=" + std::to_string(s.prov.eval_version) + "\tnne=" + s.prov.nne
             + "\tsyzygy=" + std::to_string(s.prov.syzygy) + "\tgaviota=" + std::to_string(s.prov.gaviota)
             + "\tcommit=" + s.prov.commit + "\tat=" + std::to_string(e.stored_at) + "\n";
    }

    // One line back into an entry and its key. False for a line that is not
    // one (a torn write, a hand edit): it is skipped.
    static bool parse(const std::string& line, PTT_Entry& e, Position_Key& key)
    {
        std::istringstream in(line);
        std::string field;
        bool have_fen = false, have_score = false;
        while(std::getline(in, field, '\t'))
        {
            const size_t eq = field.find('=');
            if(eq==std::string::npos)
            continue;
            const std::string k = field.substr(0, eq), v = field.substr(eq+1);
            if(k=="fen") { e.fen = v; have_fen = true; }
            else if(k=="depth") e.info.depth = std::atoi(v.c_str());
            else if(k=="score")
            {
                std::istringstream sv(v);
                have_score = (bool)(sv >> e.info.score_kind >> e.info.score_value);
            }
            else if(k=="nodes") e.info.nodes = std::atoll(v.c_str());
            else if(k=="time") e.info.time_ms = std::atoll(v.c_str());
            else if(k=="pv")
            {
                std::istringstream mv(v);
                std::string m;
                while(mv >> m)
                e.info.pv.push_back(m);
            }
            else if(k=="eval") e.info.prov.eval_version = std::atoi(v.c_str());
            else if(k=="nne") e.info.prov.nne = v;
            else if(k=="syzygy") e.info.prov.syzygy = std::atoi(v.c_str());
            else if(k=="gaviota") e.info.prov.gaviota = std::atoi(v.c_str());
            else if(k=="commit") e.info.prov.commit = v;
            else if(k=="at") e.stored_at = std::atoll(v.c_str());
        }
        if(!have_fen || !have_score || e.info.depth<=0 || e.info.pv.empty())
        return false;
        return key_of(e.fen, key);
    }

    // The position a FEN describes, as the repetition rule compares it.
    static bool key_of(const std::string& fen, Position_Key& key)
    {
        BB pos;
        if(!uci_parse_fen(fen, pos))
        return false;
        BB children[MAX_LEGAL_MOVES];
        const int n = std::get<0>(all_moves(&pos, children));
        key = position_key(pos, effective_en_passant(pos, children, n));
        return true;
    }

    private:
    std::map<Position_Key, PTT_Entry> entries;
    off_t read_offset = 0;        // how far the file has been read
    ino_t read_inode = 0;         // ...and which file that was
    long long superseded = 0;     // lines read or written that a later one replaced

    // Reads what was appended since the last read. A file shorter than that
    // was rewritten (another GUI compacted it): read it again from the start.
    void refresh()
    {
        struct stat st;
        if(stat(path.c_str(), &st)!=0 || (st.st_ino==read_inode && st.st_size==read_offset))
        return;
        if(st.st_ino!=read_inode || st.st_size<read_offset)
        {
            entries.clear();
            read_offset = 0;
            superseded = 0;
        }
        std::ifstream in(path, std::ios::binary);
        in.seekg(read_offset);
        std::string line;
        off_t consumed = read_offset;
        while(std::getline(in, line))
        {
            if(in.eof())
            break;            // a line without its newline is still being written
            consumed += line.size()+1;
            PTT_Entry e;
            Position_Key key;
            if(!parse(line, e, key))
            continue;
            auto it = entries.find(key);
            if(it==entries.end())
            entries[key] = e;
            else
            {
                superseded++;
                if(better(e, it->second))
                it->second = e;
            }
        }
        read_offset = consumed;
        read_inode = st.st_ino;
    }

    // The file at `path`, open and locked. A compaction renames a new file over
    // the one another GUI may be waiting to lock, so once the lock is held the
    // open file is checked to still be the one at the path, or opened again.
    int open_locked(int flags)
    {
        for(int tries=0; tries<10; tries++)
        {
            const int fd = ::open(path.c_str(), flags|O_CLOEXEC, 0644);
            if(fd<0)
            return -1;
            flock(fd, LOCK_EX);
            struct stat held, now;
            if(fstat(fd, &held)==0 && stat(path.c_str(), &now)==0 && held.st_ino==now.st_ino)
            return fd;
            flock(fd, LOCK_UN);
            ::close(fd);
        }
        return -1;
    }

    // Writes the live entries to a new file and renames it over the old one,
    // under the old one's lock so no append lands in between and is lost.
    void compact()
    {
        const int fd = open_locked(O_RDONLY);
        if(fd<0)
        return;
        refresh();   // whatever was appended before the lock
        const std::string tmp = path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary|std::ios::trunc);
            for(const auto& entry : entries)
            out << format(entry.second);
        }
        struct stat st;
        if(stat(tmp.c_str(), &st)==0 && std::rename(tmp.c_str(), path.c_str())==0)
        {
            read_offset = st.st_size;
            read_inode = st.st_ino;
            superseded = 0;
        }
        flock(fd, LOCK_UN);
        ::close(fd);
    }
};

// The server's one PTT, opened by main() (ptt= option).
inline PTT& persistent_tt()
{
    static PTT ptt;
    return ptt;
}

#endif // GUI_PTT_HPP
