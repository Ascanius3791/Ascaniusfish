// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

extern "C" void moncontrol(int);

static void initialize_starting_position(BB* position)
{
    initialize_FEN_to::Standartboard(position->Board);
    position->white_move=1;
    position->en_passant=0;
    position->move=1;
    position->halfmoves_since_last_capture_or_pawn_move=0;
    position->number_of_repetitions=0;
    position->is_evaluated=0;
    position->is_from_opening_book=0;
    position->eval=1000;
    position->depth_of_eval=0;
    castling_rights(position);
}

int main(int argc, char** argv)
{
    moncontrol(0);

    int iterations=1;
    int depth=100000;
    if(argc>1)
    iterations=std::atoi(argv[1]);
    if(argc>2)
    depth=std::atoi(argv[2]);
    if(iterations<1)
    iterations=1;
    if(depth<1)
    depth=1;

    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB* wfh = new BB[1024];
    Play engine;

    std::ofstream null_stream("/dev/null");
    std::streambuf* original_cout_buffer=std::cout.rdbuf();
    std::cout.rdbuf(null_stream.rdbuf());

    moncontrol(1);

    int eval_sum=0;
    for(int i=0;i<iterations;i++)
    {
        BB position;
        initialize_starting_position(&position);
        eval_sum+=engine.engine_move(&position,wfh,0,depth,WEIGHTS_OG,nullptr);
    }

    moncontrol(0);

    std::cout.rdbuf(original_cout_buffer);
    delete[] wfh;

    std::cout << "profile_startpos completed " << iterations
              << " starting-position evaluations at depth " << depth << "." << std::endl;
    std::cout << "eval checksum: " << eval_sum << std::endl;

    return 0;
}
