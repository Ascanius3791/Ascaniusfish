// OWNERSHIP=Claude
// Engine-vs-engine game from the standard start position, shown in
// display_board.py. Same setup as main() in ascaniusfish.cpp (runtime_settings.txt
// for depth/TT/time budget), but with is_human_play=0 so both sides are the engine.
// Build from the repo root and run from the repo root (the GUI script and
// runtime_settings.txt are opened by relative path):
//   g++ -O3 -g -fno-omit-frame-pointer -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG -o diagnostics/engine_vs_engine_gui diagnostics/engine_vs_engine_gui.cpp
//   ./diagnostics/engine_vs_engine_gui
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"
#include "../lib/RuntimeSettings.hpp"

int main()
{
    RuntimeSettings RS = load_runtime_settings();

    Zobrist new_zobrist = Zobrist();
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook

    const int depth=RS.depth;
    const int len=5000+depth*50;
    BB* ptr = new BB[len];
    initialize_FEN_to::Standartboard(ptr->Board);
    castling_rights(ptr);
    ptr->zobrist_hash = Zobrist::compute_Zobrist_Hash(*ptr);

    PP SP;
    SP.table = RS.use_lookup_table ? new lookup_table : nullptr;
    SP.original=ptr;
    SP.wfh=ptr+1;
    SP.depth=depth;
    SP.is_timed_move=RS.use_time_management;
    SP.time_limit_seconds=RS.time_seconds;
    SP.print_Board=1;
    SP.max_game_lengh=400;
    SP.W_white=WEIGHTS_OG;
    SP.W_black=WEIGHTS_OG;
    SP.is_human_play=0;
    SP.show_eval=1;
    SP.is_pretty_print=1;

    Play engine_game;
    engine_game.p=SP;
    int res = engine_game.nicely_written_play();
    cout << "Result code: " << res << endl;

    delete[] ptr;
    delete SP.table;
}
