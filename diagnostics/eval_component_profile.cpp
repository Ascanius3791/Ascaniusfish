// OWNERSHIP=Claude
// Times each component of basic_eval() separately over a pool of random
// mid-game positions, then reports both raw per-call cost and the weighted
// share each component contributes to a single basic_eval() call (weighted
// by how many times basic_eval() actually invokes it).
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

static void initialize_starting_position(BB* position)
{
    initialize_FEN_to::Standartboard(position->Board);
    position->white_move=1;
    position->en_passant=0;
    position->move=1;
    position->halfmoves_since_last_capture_or_pawn_move=0;
    position->number_of_repetitions=0;
    castling_rights(position);
}

// Plays out random legal moves so the position pool covers opening,
// middlegame and (occasionally) sparser endgame-ish material.
static std::vector<BB> build_position_pool(int num_games, int plies_per_game)
{
    std::vector<BB> pool;
    BB* wfh = new BB[256];

    for(int g=0; g<num_games; g++)
    {
        BB position;
        initialize_starting_position(&position);

        for(int ply=0; ply<plies_per_game; ply++)
        {
            auto generated = all_moves(&position, wfh, 256);
            int n = std::get<0>(generated);
            if(n==0)
                break;
            pool.push_back(position);
            position = wfh[std::rand()%n];
        }
        pool.push_back(position);
    }

    delete[] wfh;
    return pool;
}

template<typename F>
static double time_component(const char* name, const std::vector<BB>& pool, int repeats, F&& fn)
{
    volatile long sink=0;
    auto start = std::chrono::high_resolution_clock::now();
    for(int r=0; r<repeats; r++)
        for(const BB& pos : pool)
            sink += fn(pos);
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end-start).count();
    double ns_per_call = ms*1e6/(repeats*pool.size());
    std::cout << std::left << std::setw(28) << name
              << std::right << std::setw(10) << std::fixed << std::setprecision(1) << ms << " ms"
              << std::setw(12) << std::setprecision(1) << ns_per_call << " ns/call\n";
    return ns_per_call; // ns per single call, averaged
}

int main()
{
    std::srand(1);

    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::cout << "Building position pool...\n";
    std::vector<BB> pool = build_position_pool(/*num_games=*/8, /*plies_per_game=*/60);
    std::cout << "Pool size: " << pool.size() << " positions\n\n";

    const int repeats = 3000; // tuned to finish comfortably inside a minute

    std::cout << std::left << std::setw(28) << "component"
              << std::right << std::setw(13) << "total time"
              << std::setw(14) << "per call\n";

    double t_material = time_component("material_eval", pool, repeats,
        [](const BB& p){ return material_eval(&p, WEIGHTS_OG); });

    double t_piecetable = time_component("piecetable", pool, repeats,
        [](const BB& p){ return piecetable(&p, WEIGHTS_OG); });

    double t_king_safety = time_component("king_safety_of_colour", pool, repeats,
        [](const BB& p){ return king_safety_of_colour(p.Board, true, WEIGHTS_OG); });

    double t_positional = time_component("positional_eval (pawns)", pool, repeats,
        [](const BB& p){ return positional_eval(&p, WEIGHTS_OG); });

    double t_mobility = time_component("mobility (attacks_by_col x2)", pool, repeats,
        [](const BB& p){ return count(attacks_by_col(p.Board,1)) - count(attacks_by_col(p.Board,0)); });

    double t_activity = time_component("piece_activity_eval", pool, repeats,
        [](const BB& p){ return piece_activity_eval(&p, WEIGHTS_OG); });

    double t_full = time_component("basic_eval (whole function)", pool, repeats,
        [](const BB& p){ return basic_eval(&p, WEIGHTS_OG); });

    // basic_eval() calls king_safety_of_colour twice (once per side).
    double weighted_material   = t_material;
    double weighted_piecetable = t_piecetable;
    double weighted_king       = 2*t_king_safety;
    double weighted_positional = t_positional;
    double weighted_mobility   = t_mobility;
    double weighted_activity   = t_activity;
    double weighted_sum = weighted_material+weighted_piecetable+weighted_king
                        +weighted_positional+weighted_mobility+weighted_activity;

    std::cout << "\nEstimated share of a single basic_eval() call (component cost x call count):\n";
    auto pct = [&](double v){ return 100.0*v/weighted_sum; };
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  material_eval           x1: " << pct(weighted_material)   << "%\n";
    std::cout << "  piecetable              x1: " << pct(weighted_piecetable) << "%\n";
    std::cout << "  king_safety_of_colour   x2: " << pct(weighted_king)       << "%\n";
    std::cout << "  positional_eval(pawns)  x1: " << pct(weighted_positional) << "%\n";
    std::cout << "  mobility (attacks_by_col)x2:" << pct(weighted_mobility)   << "%\n";
    std::cout << "  piece_activity_eval     x1: " << pct(weighted_activity)   << "%\n";
    std::cout << "  (sum of parts vs measured basic_eval per-call: "
              << weighted_sum << " ns vs " << t_full << " ns)\n";

    return 0;
}
