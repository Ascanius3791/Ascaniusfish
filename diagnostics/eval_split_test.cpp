// OWNERSHIP=Claude
// The GUI's eval breakdown (gui/eval_split.hpp, #66) against the real eval:
// runs eval_self_check() with the dreamer, the same check the page runs when
// "Advanced debugging" is switched on, and prints the breakdown of a position.
// Exit code 1 on any mismatch, so it can follow an eval change.
//
//   g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses
//       -Wno-unused-variable -DNDEBUG -pthread -o diagnostics/eval_split_test diagnostics/eval_split_test.cpp
//   ./diagnostics/eval_split_test ["<fen>"]

#include "../gui/session.hpp"
#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB pos;
    const char* fen = argc > 1 ? argv[1] : "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16";
    if(!uci_parse_fen(fen, pos))
    {
        std::fprintf(stderr, "invalid FEN: %s\n", fen);
        return 2;
    }
    const Eval_Breakdown b = eval_breakdown(&pos, WEIGHTS_OG);
    std::printf("%s\n%-18s %8s %8s %8s %8s\n", fen, "term", "white", "black", "rebuilt", "real");
    for(const Eval_Row& r : b.rows)
    {
        int raw[2] = {0, 0};
        for(int k=0;k<r.split.n;k++)
        for(int side=0;side<2;side++)
        raw[side] += r.split.parts[k].raw[side];
        std::printf("%-18s %8d %8d %8d %8d%s\n", r.name, raw[1]/r.split.scale, raw[0]/r.split.scale, r.rebuilt, r.real,
                    r.rebuilt==r.real ? "" : "  MISMATCH");
    }
    std::printf("%-18s %35d %8d%s\n", "sum / basic_eval", b.real_sum, b.basic, b.real_sum==b.basic ? "" : "  MISMATCH");
    std::printf("%-18s %35d\n", "dreamer", plan_eval(&pos, WEIGHTS_OG));

    const Eval_Check c = eval_self_check(pos, true);
    std::printf("\nself-check: %d positions, %.2f ms, %s\n", c.positions, c.ms, c.ok() ? "all match" : "MISMATCH");
    for(const std::string& m : c.messages)
    std::printf("  %s\n", m.c_str());
    return c.ok() ? 0 : 1;
}
