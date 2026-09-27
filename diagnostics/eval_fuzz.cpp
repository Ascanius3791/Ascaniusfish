// OWNERSHIP=Claude
// Broader eval regression check than eval_regression_check.cpp's 30 fixed
// FENs: builds a large, seeded pool of positions from random playouts
// (openings through sparse endgames) and prints every component's value for
// each one. Meant to be diffed across a refactor to catch a case the fixed
// 30-FEN set doesn't exercise.
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdio>
#include <cstdlib>
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
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    std::srand(42);
    std::vector<BB> pool = build_position_pool(400, 40);

    int index=0;
    for(const BB& pos : pool)
    {
        int mat = material_eval(&pos, WEIGHTS_OG);
        int pt = piecetable(&pos, WEIGHTS_OG);
        int ks = king_safety_of_colour(&pos,true,WEIGHTS_OG) - king_safety_of_colour(&pos,false,WEIGHTS_OG);
        int posn = positional_eval(&pos, WEIGHTS_OG);
        int mob = 5*(count(attacked_squares(pos.Board,1))-count(attacked_squares(pos.Board,0)));
        int pa = piece_activity_eval(&pos, WEIGHTS_OG);
        int be = basic_eval(&pos, WEIGHTS_OG);
        int se = sorting_eval(&pos, WEIGHTS_OG);
        std::printf("%5d mat=%6d pt=%6d ks=%6d posn=%6d mob=%6d pa=%6d basic_eval=%8d sorting_eval=%8d\n",
                    index, mat, pt, ks, posn, mob, pa, be, se);
        index++;
    }
    std::printf("total positions: %d\n", index);
    return 0;
}
