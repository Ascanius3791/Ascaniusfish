// OWNERSHIP=Ascanius
#include "ascaniusfish.hpp"
#include "ascaniusfish_2.hpp"
#include "lib/opening_book.hpp"
#include "lib/RuntimeSettings.hpp"

// ============== OPENING BOOK CONFIGURATION ==============
const bool USE_LICHESS_JSON = true;              // true = load from Lichess JSON, false = load from PGN
const bool LOAD_FULL_OPENING_BOOK = false;       // Set to true to load entire Lichess book (one-time, slow, ~10-30 min)
const std::string OPENING_BOOK_PATH = "books/openings.json";  // Path to opening book (JSON or PGN)
const std::string OPENING_BOOK_BINARY_PATH = "books/openings.bin";  // Path to binary opening book (fast load)
const int OPENING_BOOK_MAX_POSITIONS = -1;       // Max positions to load (-1 = load all)
const int OPENING_BOOK_DEPTH = 100;              // Depth at which PGN opening book entries are stored
const int OPENING_BOOK_MAX_GAMES = -1;           // Max games to load from PGN (-1 = load all)
const int OPENING_BOOK_MAX_MOVES = 20;           // Max moves per game to load from PGN
// ========================================================

int main()
{
    RuntimeSettings RS = load_runtime_settings();

    //reset_weights_txt_and_History_of_Weight();exit(0);
    Zobrist new_zobrist = Zobrist();
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook
    ;//*pow(40,depth);//10*2*log10(depth)+500; //number of boards currently avaliable
    
    const int depth=RS.depth;//1000000;//10^7 \approx 10 seconds of thinking time
    const int len=5000+depth*50;
    BB* ptr = new BB[len];
    //initialize_FEN_to::Standartboard(ptr->Board);
    //initialize_FEN_to::ruy_lopez_berlin_defense(ptr->Board);
    std::string FEN = "2nrkb2/2pppp2/7p/8/8/P7/2PPPP2/2BRKN2 w - - 0 1";
    //std::string FEN = "2bqkb2/2pppp2/8/8/8/8/2PPPP2/2BQKB2 w - - 0 1";
    FEN_to_BB(FEN,ptr);
    //ptr->en_passant = 1ULL << 8*3+1;      
    
    castling_rights(ptr);
    ptr->zobrist_hash = Zobrist::compute_Zobrist_Hash(*ptr);
    print(ptr->Board);
    //line(ptr,ptr+1,depth,WEIGHTS_OG,INT_MIN,INT_MAX);
    //exit(0);
    

    PP SP;//standart play
    SP.table = (RS.use_lookup_table || RS.use_opening_book) ? new lookup_table : nullptr;

    // Load opening book if enabled
    if (RS.use_opening_book) {
        std::cout << "\n=== Loading Opening Book ===" << std::endl;
        bool book_loaded = false;
        
        if (LOAD_FULL_OPENING_BOOK) {
            // Load entire Lichess book (one-time setup)
            book_loaded = load_and_save_full_opening_book(
                OPENING_BOOK_PATH,
                OPENING_BOOK_BINARY_PATH,
                *SP.table,
                OPENING_BOOK_MAX_POSITIONS
            );
        } else if (USE_LICHESS_JSON) {
            // Load from Lichess JSON format
            book_loaded = load_opening_book_from_lichess_json(
                OPENING_BOOK_PATH,
                *SP.table,
                OPENING_BOOK_MAX_POSITIONS
            );
        } else {
            // Load from PGN format
            book_loaded = load_opening_book_from_pgn(
                OPENING_BOOK_PATH,
                *SP.table,
                OPENING_BOOK_DEPTH,
                OPENING_BOOK_MAX_GAMES,
                OPENING_BOOK_MAX_MOVES
            );
        }
        
        if (!book_loaded) {
            std::cerr << "Warning: Opening book failed to load. Continuing without opening book." << std::endl;
        }
        std::cout << "=== Opening Book Loaded ===\n" << std::endl;
    }
    
    SP.original=ptr;
    SP.wfh=ptr+1;
    SP.depth=depth;
    SP.is_timed_move=RS.use_time_management;
    SP.time_limit_seconds=RS.time_seconds;
    SP.print_Board=1;
    SP.max_game_lengh=400;
    SP.Number_of_games=1000;
    SP.colour=1;

    
    
    SP.W_white=WEIGHTS_OG;//.read_values_from_file("weights.txt");
    SP.W_black=WEIGHTS_OG;
    SP.is_human_play=1;
    SP.show_eval=1;
    SP.is_pretty_print=1;
    

    Play standart_engine_versus;
    standart_engine_versus.p=SP;
    BB temp;
    //FEN_to_BB("1rb2bnr/p3k1pp/2Bp1p2/q3p3/3PP3/2N2N2/PP1B1PPP/R2QK2R b KQ - 0 20",standart_engine_versus.p.original);

    //standart_engine_versus.p.W_white.read_values_from_file("weights.txt");
    //cout << "The metric is: " << standart_engine_versus.p.W_white.norm_to(WEIGHTS_OG) << endl;
    //exit(0);
    int res = standart_engine_versus.nicely_written_play();
    cout << "Pruning ratio: " << (double)pruned_moves/prunable_moves_total << endl;
    cout << "Out of " << depth*number_of_half_moves << " allowd searches, ony " << number_of_mimimax_calls << " were made" << endl;
    cout << "That is " << (double)number_of_mimimax_calls/depth/number_of_half_moves << " of the allowed searches" << endl;
    cout << "The history is: " << history.size() << "long" << endl;
    // double* accuracy = evaluate_game(history,depth);

    // cout << "White has an accuracy of: " << accuracy[0] << "%" << endl;
    // cout << "Black has an accuracy of: " << accuracy[1] << "%" << endl;
    // cout << "The total accuracy is: " << (accuracy[0]+accuracy[1])/2 << "%" << endl;
    // delete[] accuracy;
    if(SP.table)
    {
        cout << "The number of number_of_succ_readouts: " << SP.table->number_of_succ_readouts << endl;
        cout << "The number of entrys: "; SP.table->get_number_of_entrys();
        cout << "The number of insertions: " << SP.table->number_of_inserions << endl;
        cout << "The number of number_of_succ_readouts: " << SP.table->number_of_succ_readouts << endl;
        cout << "THe total number of_readouts: " << SP.table->number_of_attemted_readouts << endl;

    }
    
    
    cout << "There were " << prunable_moves_total << " prunable moves in total" << endl;
    cout << "There were " << pruned_moves << " pruned_move" << endl;
    cout << "The ratio is: " << (double)pruned_moves/prunable_moves_total << endl;
    delete[] ptr;
    delete SP.table;

}
