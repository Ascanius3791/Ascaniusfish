// OWNERSHIP=Claude
// Correctness check for the BB_lazyfields cache: every getter must agree with the raw
// (uncached) computation it wraps, across a pool of positions reached by random play.
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"
#include "../src/BB_lazyfields.cpp"

#include <cassert>
#include <cstdlib>
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

int main()
{
    std::srand(2);

    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::vector<BB> pool = build_position_pool(/*num_games=*/8, /*plies_per_game=*/60);
    std::cout << "Checking " << pool.size() << " positions...\n";

    for(BB& p : pool)
    {
        uint64_t expected_occ = p.Board[0]|p.Board[1]|p.Board[2]|p.Board[3]|p.Board[4]|p.Board[5]
                               |p.Board[6]|p.Board[7]|p.Board[8]|p.Board[9]|p.Board[10]|p.Board[11];
        assert(get_occupancy(&p) == expected_occ);
        assert(get_occupancy(&p) == expected_occ); // second call must hit the cache and still match

        uint64_t expected_white = p.Board[0]|p.Board[1]|p.Board[2]|p.Board[3]|p.Board[4]|p.Board[5];
        uint64_t expected_black = p.Board[6]|p.Board[7]|p.Board[8]|p.Board[9]|p.Board[10]|p.Board[11];
        assert(get_pieces_of_colour(&p, true) == expected_white);
        assert(get_pieces_of_colour(&p, false) == expected_black);

        assert(get_attacked_squares(&p, true) == attacked_squares(p.Board, true));
        assert(get_attacked_squares(&p, false) == attacked_squares(p.Board, false));
        assert(get_attacked_squares(&p, true) == attacked_squares(p.Board, true)); // cached path

        assert(get_in_check(&p) == in_check(p.Board, p.white_move));
        assert(get_has_legal_move(&p) == one_move(&p));
    }

    std::cout << "All BB_lazyfields getters agree with the raw computations. OK\n";
    return 0;
}
