// OWNERSHIP=Claude
// Replays a UCI line from a FEN, checks the incremental Zobrist hash against a
// full recompute at every ply, then searches the line's last position at
// depth 0..N (fresh table each time) and prints what the search makes of it.
#include "../lib/uci.hpp"

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    std::string error;
    if(nne::load("nets/nne_d6.bin", error)) nne::enabled = true;
    std::string fen = argv[1];
    int max_depth = argc>3 ? atoi(argv[3]) : 6;
    BB cur(fen);
    BB* wfh = new BB[4096];
    std::istringstream in(argv[2]);
    std::string uci;
    while(in >> uci)
    {
        auto result = all_moves(&cur, wfh);
        int n = std::get<0>(result), found=-1;
        for(int i=0;i<n;i++) if(get_UCI(&cur, wfh+i)==uci) found=i;
        if(found<0) { std::cout << "illegal " << uci << std::endl; return 1; }
        cur = wfh[found];
        uint64_t full = Zobrist::compute_Zobrist_Hash(cur);
        BB null_child(&cur, "base");
        Zobrist::update_zobrist_hash_null_move(cur, null_child);
        uint64_t null_full = Zobrist::compute_Zobrist_Hash(null_child);
        std::cout << uci << "  " << cur.get_FEN() << (full==cur.zobrist_hash ? "" : "  ZOBRIST MISMATCH")
                  << (null_full==null_child.zobrist_hash ? "" : "  NULL-MOVE ZOBRIST MISMATCH")
                  << "  null child " << null_child.get_FEN() << std::endl;
    }
    PV_Line q = minimax_tactical(&cur, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
    std::cout << "static eval " << eval(&cur, WEIGHTS_OG, 0) << "  qsearch " << q.eval << " len " << q.current_lenght << std::endl;
    for(int d=0; d<=max_depth; d++)
    {
        lookup_table* table = new lookup_table;
        BB* path_history = new BB[MAX_SEARCH_PLY];
        path_history[0] = cur;
        PV_Line pv = minimax(&cur, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, 0, nullptr);
        std::cout << "depth " << d << " eval " << pv.eval << " bound " << pv.bound_type
                  << " pv " << moves_to_PGN(cur, pv.first_n(pv.current_lenght)) << std::endl;
        delete table; delete[] path_history;
    }
}
