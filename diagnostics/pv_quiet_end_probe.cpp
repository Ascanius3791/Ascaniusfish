// OWNERSHIP=Claude
// Analyses the positions of a game one after another with ONE table, the way
// the GUI's analysis engine does, and checks every iteration's root PV: it is
// replayed to its end and the end position's quiescence value is compared with
// its static eval. A PV that ends in a position where a capture still wins a
// lot (11...Qf4?? with Bxf4 on, from the GUI) is flagged.
// usage: pv_quiet_end_probe "<start fen>" "<uci moves of the game>" <depth per position> <depth for the last>
#include "../lib/uci.hpp"

static BB* wfh;

static lookup_table* g_table;
static void check_pv(const BB& root, const PV_Line& pv, int d)
{
    BB cur = root;
    std::vector<Move> moves = pv.first_n(pv.current_lenght);
    for(const Move& m : moves)
    {
        BB child;
        make_move(&cur, m, &child);
        cur = child;
    }
    int stat = eval(&cur, WEIGHTS_OG, 0);
    PV_Line q = minimax_tactical(&cur, wfh, WEIGHTS_OG, INT_MIN, INT_MAX, nullptr);
    bool bad = q.eval != pv.eval && std::abs(q.eval) < 100000;
    std::cout << "  depth " << d << " eval " << pv.eval << " len " << pv.current_lenght
              << " end static " << stat << " end qsearch " << q.eval << (bad ? "  <<< NOT QUIET" : "")
              << "  " << moves_to_PGN(root, moves) << std::endl;
    if(!bad || !getenv("WALK")) return;
    // What the table holds for every node along the line.
    cur = root;
    for(size_t k=0;k<=moves.size();k++)
    {
        TT_readout r = g_table->is_retrivable_eval(&cur, 0);
        std::cout << "    ply " << k << (cur.white_move ? " W" : " B");
        if(r.is_found)
        std::cout << " tt depth " << r.pv_line.depth << " bound " << r.pv_line.bound_type << " eval " << r.pv_line.eval
                  << " len " << r.pv_line.current_lenght << "  " << moves_to_PGN(cur, r.pv_line.first_n(r.pv_line.current_lenght));
        else std::cout << " not in tt";
        std::cout << "  static " << eval(&cur, WEIGHTS_OG, 0) << std::endl;
        if(k==moves.size()) break;
        BB child; make_move(&cur, moves[k], &child); cur = child;
    }
}

int main(int argc, char** argv)
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    std::string error;
    if(!getenv("NO_NNE") && nne::load("nets/nne_d6.bin", error)) nne::enabled = true;
    BB cur{std::string(argv[1])};
    int depth_each = atoi(argv[3]), depth_last = atoi(argv[4]);
    wfh = new BB[4096];
    BB* gwfh = new BB[4096];
    lookup_table* table = new lookup_table;
    g_table = table;
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable* cycle_table = new CuckooCycleTable;
    std::vector<BB> game{cur};
    std::istringstream in(argv[2]);
    std::vector<std::string> ucis;
    std::string u;
    while(in >> u) ucis.push_back(u);
    for(size_t k=0; k<=ucis.size(); k++)
    {
        int max_d = k==ucis.size() ? depth_last : depth_each;
        std::cout << cur.get_FEN() << std::endl;
        table->new_search();
        int seed = std::min({(int)game.size(), cur.halfmoves_since_last_capture_or_pawn_move+1, MAX_SEARCH_PLY});
        for(int i=0;i<seed;i++) path_history[i] = game[game.size()-seed+i];
        for(int d=1; d<=max_d; d++)
        {
            PV_Line pv = minimax(&cur, gwfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table, path_history, seed-1, cycle_table);
            check_pv(cur, pv, d);
        }
        if(k==ucis.size()) break;
        auto result = all_moves(&cur, gwfh);
        int n = std::get<0>(result), found=-1;
        for(int i=0;i<n;i++) if(get_UCI(&cur, gwfh+i)==ucis[k]) found=i;
        cur = gwfh[found];
        game.push_back(cur);
    }
}
