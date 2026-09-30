// OWNERSHIP=Claude
// The engine's net against the trainer's (#52): every test position of
// nets/nne_d6_test.tsv must get the trainer's correction within 0.01 cp. Also
// checks that the engine's static eval is the dataset's, that the corrected
// score is clamped below the TB band, and times one correction.
//   make nne-test    (or: ./diagnostics/nne_inference_test [net] [tsv])
#include "../lib/uci.hpp"
#include "../lib/nne.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("  %-66s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    failures += !ok;
}

int main(int argc, char** argv)
{
    const std::string net_path = argc > 1 ? argv[1] : "nets/nne_d6.bin";
    const std::string tsv_path = argc > 2 ? argv[2] : "nets/nne_d6_test.tsv";
    constexpr double TOLERANCE = 0.01;

    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::string error;
    check(!nne::load("nets/no_such_net.bin", error) && nne::loaded_path.empty(),
          "a missing file is refused and nothing is loaded");
    if(!nne::load(net_path, error))
    {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    std::ifstream in(tsv_path);
    if(!in)
    {
        std::printf("cannot read %s\n", tsv_path.c_str());
        return 1;
    }
    std::vector<BB> positions;
    std::vector<float> expected;
    int bad_fens = 0, static_mismatches = 0, off = 0;
    double worst = 0;
    std::string worst_fen;
    for(std::string line; std::getline(in, line);)
    {
        if(line.empty() || line[0]=='#')
        continue;
        std::vector<std::string> col;
        std::stringstream ss(line);
        for(std::string c; std::getline(ss, c, '\t');)
        col.push_back(c);
        BB pos;
        if(col.size()!=5 || !uci_parse_fen(col[1], pos))
        {
            bad_fens++;
            continue;
        }
        const float trainer = std::stof(col[4]);
        const double diff = std::fabs(nne::correction(pos) - trainer);
        if(diff > worst)
        {
            worst = diff;
            worst_fen = col[1];
        }
        off += diff > TOLERANCE;
        static_mismatches += eval(&pos, WEIGHTS_OG, 0) != std::stoi(col[2]);
        positions.push_back(pos);
        expected.push_back(trainer);
    }
    std::printf("%s against %s: %zu positions, largest difference %.6f cp\n  (%s)\n",
                net_path.c_str(), tsv_path.c_str(), positions.size(), worst, worst_fen.c_str());
    check(bad_fens==0 && !positions.empty(), "every line is a readable position");
    check(off==0, "every correction within 0.01 cp of the trainer's (" + std::to_string(off) + " off)");
    check(static_mismatches==0, "the engine's static eval is the dataset's (" + std::to_string(static_mismatches) + " differ)");

    // The corrected score never reaches the TB or mate band, whichever way it is pushed.
    const BB& p = positions[0];
    check(nne::corrected_eval(&p, TB_WIN_SCORE+500)==nne::SCORE_LIMIT
       && nne::corrected_eval(&p, -TB_WIN_SCORE-500)==-nne::SCORE_LIMIT
       && !is_tb_score(nne::corrected_eval(&p, nne::SCORE_LIMIT))
       && !is_tb_score(nne::corrected_eval(&p, -nne::SCORE_LIMIT)),
          "static + correction is clamped below the TB band");
    const double c = nne::correction(p);
    check(nne::corrected_eval(&p, 37)==(int)std::lround(37 + (p.white_move ? c : -c)),
          "the correction is added in white's view");

    // Cost of one correction (features included), over the whole test set.
    const int rounds = 20;
    volatile float sink = 0;
    long long start = steady_now_ns();
    for(int r=0;r<rounds;r++)
    for(const BB& pos : positions)
    sink = sink + nne::correction(pos);
    const double ns = double(steady_now_ns()-start)/(double(rounds)*positions.size());
    std::printf("one correction: %.0f ns\n", ns);

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
