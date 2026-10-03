// OWNERSHIP=Claude
// minimax_tactical() out of budget with a skipped evasion that also loses
// (issue #78, patch (b)). Constructed: 4R2k/3K2pp/4nN1n/8/8/8/8/8 b, black in
// check from Re8 with two evasions, both mated at once:
//   ...Ng8 (searched)       Rxg8# (Nf6 guards g8)
//   ...Nf8+ (quiet, checks Kd7, so skipped at forced_moves_left 0)   Rxf8#
// At forced_moves_left 0 the skipped check-back made the result the perpetual's
// 0; with the mate check (b) it is the mate in 2 plies, which the full-width
// mate search confirms. At 1 and 2 both evasions are searched anyway.
//
//   g++ -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/quiescence_skip_mate_test diagnostics/quiescence_skip_mate_test.cpp
//   ./diagnostics/quiescence_skip_mate_test [fen="..."]
#include "../lib/uci.hpp"
#include "../lib/mate_search.hpp"

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    std::string fen = "4R2k/3K2pp/4nN1n/8/8/8/8/8 b - - 0 1";
    for(int i=1;i<argc;i++)
    {
        std::string a = argv[i];
        if(a.rfind("fen=", 0) == 0) fen = a.substr(4);
    }
    static BB wfh[4096];
    BB pos;
    uci_parse_fen(fen, pos);
    const Mate_Result proof = find_mate(pos, wfh, !pos.white_move, 20, Mate_Mode::FULL_WIDTH, 10000000);
    std::cout << fen << "\nfull-width mate search: "
              << (proof.status == Mate_Result::FOUND ? "mate in " + std::to_string(proof.plies) + " plies" : std::string("none")) << "\n";
    int failures = 0;
    for(int forced = 0; forced <= 2; forced++)
    {
        const PV_Line q = minimax_tactical(&pos, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr, forced);
        const bool mate = q.eval >= INT_MAX - max_mating_seq || q.eval <= INT_MIN + max_mating_seq;
        const int plies = q.eval >= INT_MAX - max_mating_seq ? INT_MAX - q.eval : q.eval - INT_MIN;
        std::cout << "forced_moves_left " << forced << ": ";
        if(mate) std::cout << "mate in " << plies << " plies";
        else std::cout << "eval " << q.eval;
        const bool right = proof.status == Mate_Result::FOUND && mate && plies == proof.plies;
        std::cout << (right ? "  ok" : "  NOT the mate") << "\n";
        failures += !right;
    }
    std::cout << "mate_confirmed() calls " << mate_checks << ", held " << mate_checks_confirmed << "\n";
    return failures ? 1 : 0;
}
