// OWNERSHIP=Claude
#include "../lib/gaviota.hpp"
#ifdef WITH_GAVIOTA
#include "../third_party/gaviota/gtb-probe.h"
#include <mutex>
#endif

namespace gaviota
{

#ifdef WITH_GAVIOTA

namespace detail
{
std::mutex init_mutex;
const char** paths = nullptr;
int loaded_pieces = 0;
// Board[] order is P R N B Q K; the prober's piece codes are P N B R Q K = 1..6.
constexpr unsigned char BOARD_TO_TB[6] = {tb_PAWN, tb_ROOK, tb_KNIGHT, tb_BISHOP, tb_QUEEN, tb_KING};
}

bool compiled_in() { return true; }

void release()
{
    std::lock_guard<std::mutex> lock(detail::init_mutex);
    if(detail::paths)
    {
        tbcache_done();
        tb_done();
        detail::paths = tbpaths_done(detail::paths);
        detail::paths = nullptr;
    }
    detail::loaded_pieces = 0;
}

int init(const std::string& dir, size_t cache_mb)
{
    release();
    std::lock_guard<std::mutex> lock(detail::init_mutex);
    if(dir.empty())
    return 0;
    detail::paths = tbpaths_add(tbpaths_init(), dir.c_str());
    tb_init(0, tb_CP4, detail::paths);
    tbcache_init(cache_mb*1024*1024, 0);  // all of it for DTM: WDL comes from Syzygy
    const unsigned avail = tb_availability();
    // bit 2k-4: at least one (k)-piece table, k = 3..5
    detail::loaded_pieces = (avail & 16) ? 5 : (avail & 4) ? 4 : (avail & 1) ? 3 : 0;
    return detail::loaded_pieces;
}

int max_pieces() { return detail::loaded_pieces; }

bool probe_dtm(const BB* const pos, int& result, int& plies)
{
    if(detail::loaded_pieces==0)
    return false;
    if(pos->castle[0][0] || pos->castle[0][1] || pos->castle[1][0] || pos->castle[1][1])
    return false;
    unsigned ws[MAX_PIECES+1], bs[MAX_PIECES+1];
    unsigned char wp[MAX_PIECES+1], bp[MAX_PIECES+1];
    int nw = 0, nb = 0;
    for(int i=0;i<12;i++)
    for(uint64_t b = pos->Board[i]; b; b &= b-1)
    {
        if(nw+nb==detail::loaded_pieces)
        return false;
        const unsigned sq = __builtin_ctzll(b);
        if(i<6) { ws[nw] = sq; wp[nw++] = detail::BOARD_TO_TB[i]; }
        else    { bs[nb] = sq; bp[nb++] = detail::BOARD_TO_TB[i-6]; }
    }
    ws[nw] = tb_NOSQUARE; wp[nw] = tb_NOPIECE;
    bs[nb] = tb_NOSQUARE; bp[nb] = tb_NOPIECE;
    const unsigned stm = pos->white_move ? tb_WHITE_TO_MOVE : tb_BLACK_TO_MOVE;
    const unsigned ep = pos->en_passant ? (unsigned)__builtin_ctzll(pos->en_passant) : (unsigned)tb_NOSQUARE;
    unsigned info = tb_UNKNOWN, p = 0;
    if(!tb_probe_hard(stm, ep, tb_NOCASTLE, ws, bs, wp, bp, &info, &p))
    return false;
    if(info==tb_DRAW)
    {
        result = 0;
        plies = 0;
        return true;
    }
    if(info!=tb_WMATE && info!=tb_BMATE)
    return false;
    const bool white_mates = info==tb_WMATE;
    result = white_mates==pos->white_move ? 1 : -1;
    plies = (int)p;
    return true;
}

#else  // no prober in this build

bool compiled_in() { return false; }
void release() {}
int init(const std::string&, size_t) { return 0; }
int max_pieces() { return 0; }
bool probe_dtm(const BB* const, int&, int&) { return false; }

#endif

} // namespace gaviota
