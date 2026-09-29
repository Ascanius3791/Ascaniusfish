// OWNERSHIP=Claude
#ifndef TIME_MANAGER_CPP
#define TIME_MANAGER_CPP
#include "../lib/time_manager.hpp"
#include <algorithm>

int expected_moves_left(const BB& pos)
{
    float material = (enemy_material_left_percent(&pos, true) + enemy_material_left_percent(&pos, false))/2;
    int eval_cp = eval(&pos, WEIGHTS_OG);
    double estimate = 20 + material*20 - std::abs(eval_cp)/200.0;
    return std::max((double)TM_MIN_MOVES_LEFT, estimate);
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

void Lambda_History::push(double raw_lambda)
{
    int n = std::min(count+1, TM_LAMBDA_HISTORY);
    for(int i=n-1; i>0; i--)
    values[i] = values[i-1];
    values[0] = raw_lambda;
    count = n;
}

double Lambda_History::average() const
{
    if(count==0)
    return 1;
    double sum = 0, weight = 0.5, total_weight = 0;
    for(int i=0; i<count; i++, weight/=2)
    {
        sum += weight*values[i];
        total_weight += weight;
    }
    return sum/total_weight;
}

TimeManager::TimeManager(const BB& root, long long time_ms, long long inc_ms, int movestogo, Lambda_History& hist)
: history(hist)
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
    pv[depth][k] = line.at(k);
    finished_at[depth] = elapsed_ms;
    deepest = depth;
    if(!TM_PV_STABILITY)
    {
        raw_lam = lam = 1;
        return;
    }
    raw_lam = pv_instability(pv, len, deepest);
    double avg = std::max(1e-9, history.average());
    lam = std::clamp(raw_lam/avg, 0.0, (double)TM_HARD_SHARES);
}

bool TimeManager::start_next_iteration(long long elapsed_ms) const
{
    long long last = finished_at[deepest] - (deepest>1 ? finished_at[deepest-1] : 0);
    return elapsed_ms + TM_ITERATION_GROWTH*last <= soft_ms();
}

#endif // TIME_MANAGER_CPP
