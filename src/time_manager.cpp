// OWNERSHIP=Claude
#ifndef TIME_MANAGER_CPP
#define TIME_MANAGER_CPP
#include "../lib/time_manager.hpp"
#include <algorithm>

int expected_moves_left(const BB& pos)
{
    return TM_DEFAULT_MOVES_LEFT;
}

double pv_instability(const Move pv[][TM_PV_LEN], const int* len, int deepest)
{
    if(deepest<2)
    return 1;
    double sum = 0, weights = 0;
    for(int d=1; d<deepest; d++)
    {
        int l = std::min(len[d], len[d+1]);
        double agreement = 0, bit = 0.5;
        for(int k=0; k<l; k++, bit/=2)
        if(pv[d][k]==pv[d+1][k])
        agreement += bit;
        double w = 1.0/(deepest-(d+1)+1);
        sum += w*(1-agreement);
        weights += w;
    }
    return sum/weights;
}

TimeManager::TimeManager(const BB& root, long long time_ms, long long inc_ms, int movestogo)
{
    time_ms = std::max(0LL, time_ms);
    inc = std::max(0LL, inc_ms);
    int moves_left = movestogo>0 ? movestogo : expected_moves_left(root);
    share = time_ms/moves_left;
    long long reserve = std::min(TM_MOVE_OVERHEAD_MS, time_ms/2);
    hard = std::max(1LL, std::min(inc + TM_HARD_SHARES*share, time_ms-reserve));
}

long long TimeManager::soft_ms() const
{
    return std::min(hard, inc + (long long)(lam*share));
}

void TimeManager::iteration_done(int depth, const PV_Line& line, long long elapsed_ms)
{
    if(depth<1 || depth>TM_MAX_DEPTH)
    return;
    len[depth] = std::min(line.current_lenght, TM_PV_LEN);
    for(int k=0; k<len[depth]; k++)
    pv[depth][k] = line.moves[k];
    finished_at[depth] = elapsed_ms;
    deepest = depth;
    lam = TM_PV_STABILITY ? pv_instability(pv, len, deepest) : 1;
}

bool TimeManager::start_next_iteration(long long elapsed_ms) const
{
    long long last = finished_at[deepest] - (deepest>1 ? finished_at[deepest-1] : 0);
    return elapsed_ms + TM_ITERATION_GROWTH*last <= soft_ms();
}

#endif // TIME_MANAGER_CPP
