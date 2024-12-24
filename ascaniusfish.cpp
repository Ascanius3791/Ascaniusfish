#include "ascaniusfish.hpp"
#include "ascaniusfish_2.hpp"

int main()
{
    
    //reset_weights_txt_and_History_of_Weight();exit(0);
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook
    //PRESENT present;
    //present.play();exit(0);
    
    
    const int depth=1000000;//4000;
    const int len=2*depth+500; //number of boards currently avaliable

    
    BB* ptr = new BB[len];
    initialize_FEN_to::Standartboard(ptr->Board);
    //FEN_to_BB("r1b1kb1r/p1p2pp1/2p5/4p2p/2p3n1/2N2N1P/PP2PPP1/R1B1KB1R w kq h6 0 19",ptr);
    int counter=0;
    
    castling_rights(ptr);
    print(ptr->Board);
    //line(ptr,ptr+1,depth,WEIGHTS_OG,INT_MIN,INT_MAX);
    //exit(0);
    

    PP SP;//standart play
    //SP.table= new lookup_table;
    SP.original=ptr;
    SP.wfh=ptr+1;
    SP.depth=depth;
    SP.print_Board=1;
    SP.max_game_lengh=200;
    SP.Number_of_games=10000;
    SP.colour=1;

    
    
    SP.W_white.read_values_from_file("weights.txt");
    SP.W_black=WEIGHTS_OG;
    SP.is_human_play=1;
    SP.show_eval=1;
    SP.is_pretty_print=1;
    

    Play standart_engine_versus;
    standart_engine_versus.p=SP;
    BB temp;
    FEN_to_BB("1rb2bnr/p3k1pp/2Bp1p2/q3p3/3PP3/2N2N2/PP1B1PPP/R2QK2R b KQ - 0 20",standart_engine_versus.p.original);

    //standart_engine_versus.p.W_white.read_values_from_file("weights.txt");
    //cout << "The metric is: " << standart_engine_versus.p.W_white.norm_to(WEIGHTS_OG) << endl;
    //exit(0);
    int res = standart_engine_versus.nicely_written_play();
    cout << "Pruning ratio: " << (double)pruned_moves/prunable_moves_total << endl;
    cout << "Out of " << depth*number_of_half_moves << " allowd searches, ony " << number_of_mimimax_calls << " were made" << endl;
    cout << "That is " << (double)number_of_mimimax_calls/depth/number_of_half_moves << " of the allowed searches" << endl;
    cout << "The history is: " << history.size() << "long" << endl;
    double* accuracy = evaluate_game(history,depth);

    cout << "White has an accuracy of: " << accuracy[0] << "%" << endl;
    cout << "Black has an accuracy of: " << accuracy[1] << "%" << endl;
    cout << "The total accuracy is: " << (accuracy[0]+accuracy[1])/2 << "%" << endl;
    delete[] accuracy;
    exit(0);
    cout << "The number of number_of_succ_readouts: " << SP.table->number_of_succ_readouts << endl;
    //SP.table->reset();
    res = standart_engine_versus.nicely_written_play();
    cout << "The result was: " << res << endl;
        
    if(SP.table)
    {
        cout << "The number of entrys: "; SP.table->get_number_of_entrys();
        cout << "The number of insertions: " << SP.table->number_of_inserions << endl;
        cout << "The number of number_of_succ_readouts: " << SP.table->number_of_succ_readouts << endl;
        cout << "THe total number of_readouts: " << SP.table->number_of_attemted_readouts << endl;

    }
    
    
    cout << "There were " << prunable_moves_total << " prunable moves in total" << endl;
    cout << "There were " << pruned_moves << " pruned_move" << endl;
    cout << "The ratio is: " << (double)pruned_moves/prunable_moves_total << endl;
    delete[] ptr;
    //delete SP.table;

}
