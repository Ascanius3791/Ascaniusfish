// OWNERSHIP=Claude
// The dreamer's fast plan term (src/plan_eval.cpp) against its reference, which
// re-evaluates every target, on positions from a tune set with the default set
// and with a set whose mobility tables and queen weights are scrambled (#90: the
// tables make the activity nonlinear, so linear tables would hide a wrong
// incremental mobility). Run it with both settings of PLAN_OWN_ACTIVITY.
// Exit code 1 on a mismatch.
//
//   g++ -O2 -mpopcnt -fwhole-program -w -DNDEBUG -pthread [-DPLAN_OWN_ACTIVITY=0] -o diagnostics/plan_mobility_check diagnostics/plan_mobility_check.cpp
//   ./diagnostics/plan_mobility_check [data/tune/test.tsv] [positions]

#include "../gui/session.hpp"
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    const std::string file = argc > 1 ? argv[1] : "data/tune/test.tsv";
    const int limit = argc > 2 ? std::atoi(argv[2]) : 5000;

    WEIGHTS scrambled = WEIGHTS_OG;
    std::mt19937 rng(90);
    std::uniform_int_distribution<int> d(-60, 60);
    for(int* t : {scrambled.mobility_knight_opening, scrambled.mobility_knight_endgame}) for(int i=0;i<9;i++) t[i] = d(rng);
    for(int* t : {scrambled.mobility_bishop_opening, scrambled.mobility_bishop_endgame}) for(int i=0;i<14;i++) t[i] = d(rng);
    for(int* t : {scrambled.mobility_rook_opening, scrambled.mobility_rook_endgame}) for(int i=0;i<15;i++) t[i] = d(rng);
    for(int* t : {scrambled.mobility_queen_opening, scrambled.mobility_queen_endgame}) for(int i=0;i<28;i++) t[i] = d(rng);
    scrambled.activity_queen_defend = d(rng);
    scrambled.activity_queen_attack = d(rng);

    std::ifstream in(file);
    if(!in) { std::fprintf(stderr, "cannot read %s\n", file.c_str()); return 1; }
    std::string line;
    std::getline(in, line);// header
    int n = 0, bad[2] = {0, 0};
    while(n < limit && std::getline(in, line))
    {
        std::istringstream f(line);
        std::string game, ply, fen;
        std::getline(f, game, '\t');
        std::getline(f, ply, '\t');
        std::getline(f, fen, '\t');
        BB pos;
        if(!uci_parse_fen(fen, pos))
        continue;
        n++;
        for(int k=0;k<2;k++)
        {
            const WEIGHTS& W = k ? scrambled : WEIGHTS_OG;
            const int fast = plan_eval_detail(&pos, W, nullptr, nullptr, false, PLAN_Q2_MODE, false);
            const int ref = plan_eval_detail(&pos, W, nullptr, nullptr, true, PLAN_Q2_MODE, false);
            if(fast != ref && bad[k]++ < 3)
            std::printf("%s: fast %d, reference %d: %s\n", k ? "scrambled" : "default", fast, ref, fen.c_str());
        }
    }
    std::printf("PLAN_OWN_ACTIVITY %d, %d positions: %d mismatches with the default set, %d with scrambled mobility\n",
                (int)PLAN_OWN_ACT, n, bad[0], bad[1]);
    return bad[0] || bad[1];
}
