// OWNERSHIP=Claude
#include "ascaniusfish.hpp"
#include "ascaniusfish_2.hpp"

#include <climits>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

static double percentage(int hits, int attempts)
{
    if(attempts==0)
    return 0.0;
    return 100.0*hits/attempts;
}

static void initialize_starting_position(BB* position)
{
    initialize_FEN_to::Standartboard(position->Board);
    position->white_move=1;
    position->en_passant=0;
    position->move=1;
    position->halfmoves_since_last_capture_or_pawn_move=0;
    position->is_evaluated=0;
    position->eval=1000;
    position->depth_of_eval=0;
    castling_rights(position);
}

static int play_game_pass(
    int pass,
    int max_halfmoves,
    int depth,
    lookup_table* table,
    Play* engine,
    BB* wfh,
    std::ofstream* null_stream,
    std::streambuf* original_cout_buffer)
{
    BB position;
    initialize_starting_position(&position);

    std::cout << "Pass " << pass << std::endl;
    std::cout << "ply,move,eval,attempts_delta,hits_delta,insertions_delta,minimax_calls_delta,milliseconds,total_attempts,total_hits,total_insertions,hit_rate" << std::endl;

    int played_halfmoves=0;
    for(int ply=1;ply<=max_halfmoves;ply++)
    {
        if(result(&position)!=0)
        break;

        BB before=position;
        const int attempts_before=table->number_of_attemted_readouts;
        const int hits_before=table->number_of_succ_readouts;
        const int insertions_before=table->number_of_inserions;
        const int minimax_calls_before=number_of_mimimax_calls;
        const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();

        std::cout.rdbuf(null_stream->rdbuf());
        const int eval=engine->engine_move(&position,wfh,0,depth,WEIGHTS_OG,table);
        std::cout.rdbuf(original_cout_buffer);

        const std::chrono::steady_clock::time_point stop=std::chrono::steady_clock::now();
        const long long milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(stop-start).count();

        const int attempts_delta=table->number_of_attemted_readouts-attempts_before;
        const int hits_delta=table->number_of_succ_readouts-hits_before;
        const int insertions_delta=table->number_of_inserions-insertions_before;
        const int minimax_calls_delta=number_of_mimimax_calls-minimax_calls_before;
        const std::string move=get_move(&before,&position);
        played_halfmoves++;

        std::cout << ply << ","
                  << move << ","
                  << eval << ","
                  << attempts_delta << ","
                  << hits_delta << ","
                  << insertions_delta << ","
                  << minimax_calls_delta << ","
                  << milliseconds << ","
                  << table->number_of_attemted_readouts << ","
                  << table->number_of_succ_readouts << ","
                  << table->number_of_inserions << ","
                  << std::fixed << std::setprecision(2)
                  << percentage(table->number_of_succ_readouts,table->number_of_attemted_readouts)
                  << "%"
                  << std::endl;
    }

    std::cout << "Pass " << pass << " summary: "
              << table->number_of_succ_readouts << " cumulative successful readouts out of "
              << table->number_of_attemted_readouts << " attempted readouts, with "
              << table->number_of_inserions << " insertions, after "
              << played_halfmoves << " halfmoves ("
              << std::fixed << std::setprecision(2)
              << percentage(table->number_of_succ_readouts,table->number_of_attemted_readouts)
              << "% cumulative hit rate)." << std::endl;

    return played_halfmoves;
}

int main(int argc, char** argv)
{
    int max_halfmoves=12;
    int depth=2;
    if(argc>1)
    max_halfmoves=std::atoi(argv[1]);
    if(argc>2)
    depth=std::atoi(argv[2]);
    if(max_halfmoves<1)
    max_halfmoves=1;
    if(depth<1)
    depth=1;

    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    lookup_table* table = new lookup_table;
    BB* wfh = new BB[200000];
    Play engine;

    std::ofstream null_stream("/dev/null");
    std::streambuf* original_cout_buffer=std::cout.rdbuf();

    std::cout << "Starting position warmed hash-table game test" << std::endl;
    std::cout << "halfmoves per pass: " << max_halfmoves << ", depth: " << depth << std::endl;
    std::cout << "Pass 2 reuses the table filled by pass 1." << std::endl;

    play_game_pass(1,max_halfmoves,depth,table,&engine,wfh,&null_stream,original_cout_buffer);

    const int attempts_after_pass_1=table->number_of_attemted_readouts;
    const int hits_after_pass_1=table->number_of_succ_readouts;
    const int insertions_after_pass_1=table->number_of_inserions;
    const int minimax_calls_after_pass_1=number_of_mimimax_calls;

    play_game_pass(2,max_halfmoves,depth,table,&engine,wfh,&null_stream,original_cout_buffer);

    const int pass_2_attempts=table->number_of_attemted_readouts-attempts_after_pass_1;
    const int pass_2_hits=table->number_of_succ_readouts-hits_after_pass_1;
    const int pass_2_insertions=table->number_of_inserions-insertions_after_pass_1;
    const int pass_2_minimax_calls=number_of_mimimax_calls-minimax_calls_after_pass_1;

    std::cout.rdbuf(original_cout_buffer);
    delete[] wfh;

    std::cout << "Warm-cache summary: pass 2 had "
              << pass_2_hits << " successful readouts out of "
              << pass_2_attempts << " attempted readouts, with "
              << pass_2_insertions << " new insertions and "
              << pass_2_minimax_calls << " minimax calls ("
              << std::fixed << std::setprecision(2)
              << percentage(pass_2_hits,pass_2_attempts)
              << "% pass-2 hit rate)." << std::endl;

    delete table;

    return 0;
}
