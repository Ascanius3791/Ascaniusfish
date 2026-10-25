// OWNERSHIP=Claude
// The tuner's binary data (#116): see lib/game_pack.hpp for the format.
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char PACK_MAGIC[8] = { 'A', 'F', 'G', 'P', 'A', 'C', 'K', 0 };
constexpr uint32_t PACK_VERSION = 1;
constexpr size_t PACK_HEADER_BYTES = 8 + 4 + 4 + 8 + 8*PACK_SETS;
constexpr size_t PACK_GAME_BYTES = 4 + 2 + 2 + 1 + 1 + 2 + 2;

template<class T> static T pack_get(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
template<class T> static void pack_put(std::string& out, T v) { out.append((const char*)&v, sizeof v); }

bool pack_position(const BB& pos, Packed_Pos& out)
{
    std::memset(&out, 0, sizeof out);
    uint64_t occ = 0;
    for(int i=0;i<12;i++) occ |= pos.Board[i];
    if(__builtin_popcountll(occ)>32 || pos.halfmoves_since_last_capture_or_pawn_move<0 || pos.halfmoves_since_last_capture_or_pawn_move>65535
       || pos.move<0 || pos.move>65535 || __builtin_popcountll(pos.en_passant)>1)
    return false;
    out.occupancy = occ;
    int k = 0;
    for(uint64_t o=occ; o; o&=o-1, k++)
    {
        const uint64_t sq = o & -o;
        int piece = 0;
        while(!(pos.Board[piece] & sq)) piece++;
        out.pieces[k/2] |= piece << 4*(k&1);
    }
    out.flags = pos.white_move | pos.castle[1][1]<<1 | pos.castle[1][0]<<2 | pos.castle[0][1]<<3 | pos.castle[0][0]<<4;
    out.en_passant = pos.en_passant ? __builtin_ctzll(pos.en_passant) : 64;
    out.halfmoves = (uint16_t)pos.halfmoves_since_last_capture_or_pawn_move;
    out.fullmove = (uint16_t)pos.move;
    return true;
}

void unpack_position(const Packed_Pos& in, BB& out)
{
    for(int i=0;i<12;i++) out.Board[i] = 0;
    int k = 0;
    for(uint64_t o=in.occupancy; o; o&=o-1, k++)
    out.Board[in.pieces[k/2] >> 4*(k&1) & 15] |= o & -o;
    out.white_move = in.flags & 1;
    out.castle[1][1] = in.flags>>1 & 1;
    out.castle[1][0] = in.flags>>2 & 1;
    out.castle[0][1] = in.flags>>3 & 1;
    out.castle[0][0] = in.flags>>4 & 1;
    out.en_passant = in.en_passant<64 ? 1ULL << in.en_passant : 0;
    out.halfmoves_since_last_capture_or_pawn_move = in.halfmoves;
    out.move = in.fullmove;
    out.number_of_repetitions = 0;
    out.lazy.reset();
    out.zobrist_hash = Zobrist::compute_Zobrist_Hash(out);
}

bool operator==(const Packed_Pos& a, const Packed_Pos& b) { return std::memcmp(&a, &b, sizeof a)==0; }

Pack_Mark Pack_Game::mark(int k) const
{
    const uint8_t* p = marks + k*PACK_MARK_BYTES;
    return { p[0], pack_get<uint16_t>(p+1), pack_get<uint32_t>(p+3) };
}

Game_Pack::~Game_Pack()
{
    if(data) munmap((void*)data, bytes);
}

bool Game_Pack::open(const std::string& path, std::string& error)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if(fd<0) { error = "cannot open " + path; return false; }
    struct stat st;
    if(fstat(fd, &st)!=0 || st.st_size<(off_t)PACK_HEADER_BYTES) { ::close(fd); error = path + " is not a game pack (too short)"; return false; }
    bytes = st.st_size;
    void* m = mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if(m==MAP_FAILED) { error = "cannot map " + path; return false; }
    data = (const uint8_t*)m;
    madvise(m, bytes, MADV_SEQUENTIAL);
    if(std::memcmp(data, PACK_MAGIC, 8)!=0) { error = path + " is not a game pack (no AFGPACK magic)"; return false; }
    const uint32_t version = pack_get<uint32_t>(data+8), tag_bytes = pack_get<uint32_t>(data+12);
    if(version!=PACK_VERSION) { error = path + ": pack version " + std::to_string(version) + ", this build reads " + std::to_string(PACK_VERSION); return false; }
    const uint64_t games = pack_get<uint64_t>(data+16);
    for(int s=0;s<PACK_SETS;s++) n_marks[s] = pack_get<uint64_t>(data+24+8*s);
    size_t p = PACK_HEADER_BYTES;
    if(p+tag_bytes>bytes) { error = path + ": truncated header"; return false; }
    tag_text.assign((const char*)data+p, tag_bytes);
    p += tag_bytes;
    at.reserve(games);
    uint64_t counted[PACK_SETS] = {0, 0, 0, 0};
    for(uint64_t g=0; g<games; g++)
    {
        if(p+PACK_GAME_BYTES>bytes) { error = path + ": truncated at game " + std::to_string(g); return false; }
        const size_t plies = pack_get<uint16_t>(data+p+10), n = pack_get<uint16_t>(data+p+12);
        const size_t end = p + PACK_GAME_BYTES + plies + n*PACK_MARK_BYTES;
        if(end>bytes) { error = path + ": truncated at game " + std::to_string(g); return false; }
        at.push_back(p);
        const Pack_Game G = game(at.size()-1);
        for(int k=0;k<G.n_marks;k++)
        {
            const Pack_Mark M = G.mark(k);
            if(M.set>=PACK_SETS || M.ply>G.plies) { error = path + ": bad mark in game " + std::to_string(G.number); return false; }
            counted[M.set]++;
        }
        p = end;
    }
    if(p!=bytes) { error = path + ": " + std::to_string(bytes-p) + " bytes after the last game"; return false; }
    for(int s=0;s<PACK_SETS;s++)
    if(counted[s]!=n_marks[s]) { error = path + ": the header's mark counts do not match the games"; return false; }
    return true;
}

Pack_Game Game_Pack::game(size_t i) const
{
    return read_game(data + at[i]);
}

Pack_Game read_game(const uint8_t* p)
{
    Pack_Game g;
    g.number = pack_get<uint32_t>(p);
    g.white_elo = pack_get<uint16_t>(p+4);
    g.black_elo = pack_get<uint16_t>(p+6);
    g.result = p[8];
    g.plies = pack_get<uint16_t>(p+10);
    g.n_marks = pack_get<uint16_t>(p+12);
    g.moves = p + PACK_GAME_BYTES;
    g.marks = g.moves + g.plies;
    return g;
}

int move_rank(const BB& pos, const BB& child)
{
    Move_List list;
    const int n = generate_legal_moves(&pos, list, GEN_ALL);
    int found = -1;
    BB c;
    for(int i=0;i<n && found<0;i++)
    {
        make_move(&pos, list[i], &c);
        if(std::memcmp(c.Board, child.Board, sizeof c.Board)==0) found = i;
    }
    if(found<0) return -1;
    int rank = 0;
    for(int i=0;i<n;i++) rank += move_key(list[i])<move_key(list[found]);
    return rank;
}

bool play_rank(const BB& pos, int rank, BB& child)
{
    Move_List list;
    const int n = generate_legal_moves(&pos, list, GEN_ALL);
    if(rank>=n) return false;
    uint32_t keys[MOVE_LIST_CAP];
    for(int i=0;i<n;i++) keys[i] = (uint32_t)move_key(list[i])<<8 | i;
    std::nth_element(keys, keys+rank, keys+n);
    make_move(&pos, list[keys[rank] & 255], &child);
    return true;
}

// pos as uci_parse_fen() builds it from its FEN: the en passant square only when a
// pawn can take there, the game's clocks.
static void pack_settle(BB& pos, int halfmoves, int ply)
{
    if(pos.en_passant)
    {
        Move_List list;
        const int n = generate_legal_moves(&pos, list, GEN_CAPTURES);
        const uint64_t pawns = pos.Board[pos.white_move ? 0 : 6];
        bool legal = false;
        for(int i=0;i<n && !legal;i++)
        legal = (1ULL<<list[i].to)==pos.en_passant && (pawns>>list[i].from & 1);
        if(!legal) pos.en_passant = 0;
    }
    pos.halfmoves_since_last_capture_or_pawn_move = halfmoves;
    pos.move = 1 + ply/2;
    pos.number_of_repetitions = 0;
    pos.lazy.reset();
    pos.zobrist_hash = Zobrist::compute_Zobrist_Hash(pos);
}

static const BB& pack_start()
{
    static const BB start = [] { BB b; uci_parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", b); return b; }();
    return start;
}

template<class F>
bool replay(const Pack_Game& g, unsigned sets, F&& f, std::string& error)
{
    int last = -1;
    for(int k=0;k<g.n_marks;k++)
    {
        const Pack_Mark m = g.mark(k);
        if(sets>>m.set & 1) last = std::max(last, m.ply);
    }
    if(last<0) return true;
    BB pos = pack_start(), next;
    int halfmoves = 0, k = 0;
    for(int ply=0; ; ply++)
    {
        for(; k<g.n_marks && g.mark(k).ply==ply; k++)
        {
            const Pack_Mark m = g.mark(k);
            if(!(sets>>m.set & 1)) continue;
            BB settled = pos;
            pack_settle(settled, halfmoves, ply);
            f(m, (const BB&)settled);
        }
        if(ply>=last) return true;
        if(!play_rank(pos, g.moves[ply], next))
        {
            error = "game " + std::to_string(g.number) + ": move rank " + std::to_string(g.moves[ply]) + " out of range at ply " + std::to_string(ply);
            return false;
        }
        const int own = pos.white_move ? 0 : 6;
        uint64_t before = 0, after = 0;
        for(int i=0;i<12;i++) { before |= pos.Board[i]; after |= next.Board[i]; }
        const bool reset = pos.Board[own]!=next.Board[own] || __builtin_popcountll(after)<__builtin_popcountll(before);
        halfmoves = reset ? 0 : halfmoves+1;
        pos = next;
    }
}

void append_game(std::string& out, uint32_t number, int white_elo, int black_elo, int result,
                 const std::vector<uint8_t>& ranks, std::vector<Pack_Mark> marks)
{
    std::stable_sort(marks.begin(), marks.end(), [](const Pack_Mark& a, const Pack_Mark& b) { return a.ply<b.ply; });
    pack_put<uint32_t>(out, number);
    pack_put<uint16_t>(out, (uint16_t)white_elo);
    pack_put<uint16_t>(out, (uint16_t)black_elo);
    pack_put<uint8_t>(out, (uint8_t)result);
    pack_put<uint8_t>(out, 0);
    pack_put<uint16_t>(out, (uint16_t)ranks.size());
    pack_put<uint16_t>(out, (uint16_t)marks.size());
    out.append((const char*)ranks.data(), ranks.size());
    for(const Pack_Mark& m : marks)
    {
        pack_put<uint8_t>(out, (uint8_t)m.set);
        pack_put<uint16_t>(out, (uint16_t)m.ply);
        pack_put<uint32_t>(out, m.index);
    }
}

bool Pack_Writer::open(const std::string& to, const std::string& tag, std::string& error)
{
    path = to;
    file = std::fopen(path.c_str(), "wb");
    if(!file) { error = "cannot write " + path; return false; }
    std::string head(PACK_MAGIC, 8);
    pack_put<uint32_t>(head, PACK_VERSION);
    pack_put<uint32_t>(head, (uint32_t)tag.size());
    head.append(8 + 8*PACK_SETS, '\0');   // the counts, from close()
    head += tag;
    ok = std::fwrite(head.data(), 1, head.size(), file)==head.size();
    return ok;
}

bool Pack_Writer::add(const std::string& records, size_t n, const uint64_t marks[PACK_SETS])
{
    ok = ok && std::fwrite(records.data(), 1, records.size(), file)==records.size();
    games += n;
    for(int s=0;s<PACK_SETS;s++) n_marks[s] += marks[s];
    return ok;
}

bool Pack_Writer::close(std::string& error)
{
    std::string counts;
    pack_put<uint64_t>(counts, games);
    for(int s=0;s<PACK_SETS;s++) pack_put<uint64_t>(counts, n_marks[s]);
    ok = ok && std::fseek(file, 16, SEEK_SET)==0 && std::fwrite(counts.data(), 1, counts.size(), file)==counts.size();
    ok = std::fclose(file)==0 && ok;
    file = nullptr;
    if(!ok) error = "write error on " + path;
    return ok;
}
