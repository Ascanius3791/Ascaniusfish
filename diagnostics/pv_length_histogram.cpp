// OWNERSHIP=Claude
// One-off measurement for the "shrink PV_Line" design question: after a real
// search, how long are the PV lines actually stored in the transposition table,
// and at what search depth? Answers (a) does depth==1/short-line dominate,
// (b) where would an inline-8 + heap-extension cut land, (c) how much of
// TT_entry would a move-only entry save. Not part of make tests.
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

// The table array is protected; expose a read-only scan over it.
class Probed_TT : public lookup_table
{
    public:
    void histogram() const
    {
        long long by_len[MAX_PV_Lenght+1] = {0};
        long long by_depth[64] = {0};
        long long initialized = 0, len_gt_8 = 0, len_gt_16 = 0, sum_len = 0, max_len = 0;
        for(int h=0; h<size; h++)
        for(int b=0; b<table[h].fill_count(); b++)
        {
            const TT_slot& e = table[h].slot[b];
            initialized++;
            int l = e.pv_line.current_lenght;
            if(l<0) l=0;
            if(l>MAX_PV_Lenght) l=MAX_PV_Lenght;
            by_len[l]++;
            sum_len += l;
            if(l>max_len) max_len=l;
            if(l>8) len_gt_8++;
            if(l>16) len_gt_16++;
            int d = e.pv_line.depth;
            if(d>=0 && d<64) by_depth[d]++;
        }
        std::cout << "initialized entries: " << initialized << "\n";
        if(!initialized) return;
        std::cout << "mean PV length: " << (double)sum_len/initialized
                  << "   max: " << max_len << "\n";
        std::cout << "\nPV length histogram (len: count, share)\n";
        for(int l=0; l<=max_len && l<=32; l++)
        if(by_len[l])
        std::cout << "  " << l << ": " << by_len[l]
                  << "  (" << 100.0*by_len[l]/initialized << "%)\n";
        std::cout << "\nstored depth histogram\n";
        for(int d=0; d<64; d++)
        if(by_depth[d])
        std::cout << "  depth " << d << ": " << by_depth[d]
                  << "  (" << 100.0*by_depth[d]/initialized << "%)\n";
        std::cout << "\nlen>8:  " << len_gt_8  << "  (" << 100.0*len_gt_8/initialized  << "%)\n";
        std::cout << "len>16: " << len_gt_16 << "  (" << 100.0*len_gt_16/initialized << "%)\n";
        std::cout << "extension chunks in use: " << pv_extension_pool().in_use()
                  << "   high water: " << pv_extension_pool().high_water()
                  << " / " << pv_extension_pool().capacity() << "\n";
    }
};

int main(int argc, char** argv)
{
    initialize_rand();
    Zobrist dummy_zobrist_init; // populates the static Zobrist key tables
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    const int depth = (argc>1) ? std::atoi(argv[1]) : 6;
    const std::string fen = (argc>2) ? argv[2]
        : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    std::cout << "sizeof(Move)     = " << sizeof(Move) << "\n";
    std::cout << "sizeof(PV_Line)  = " << sizeof(PV_Line) << "\n";
    std::cout << "sizeof(TT_entry) = " << sizeof(TT_entry) << "\n";
    std::cout << "MAX_PV_Lenght    = " << MAX_PV_Lenght << "\n\n";

    BB root;
    FEN_to_BB(fen, &root);
    castling_rights(&root);
    root.zobrist_hash = Zobrist::compute_Zobrist_Hash(root);

    Probed_TT* table = new Probed_TT();
    BB* wfh = new BB[MAX_SEARCH_PLY*2];
    BB* path_history = new BB[MAX_SEARCH_PLY];
    CuckooCycleTable cycle_table;

    // Iterative deepening, like the real UCI search, so the table holds the
    // mix of depths a real game actually leaves behind.
    for(int d=1; d<=depth; d++)
    {
        table->new_search();
        PV_Line pv = minimax(&root, wfh, d, WEIGHTS_OG, INT_MIN, INT_MAX, table,
                             path_history, 0, &cycle_table);
        std::cout << "depth " << d << ": eval " << pv.eval
                  << "  pv len " << pv.current_lenght
                  << "  minimax calls " << number_of_mimimax_calls
                  << "  inserts " << table->number_of_inserions
                  << "  pruned " << pruned_moves << "/" << prunable_moves_total << "\n";
    }
    std::cout << "\ninsertions: " << table->number_of_inserions
              << "  attempted readouts: " << table->number_of_attemted_readouts
              << "  successful: " << table->number_of_succ_readouts << "\n";
    table->get_number_of_entrys();
    std::cout << "\n";
    table->histogram();

    delete[] path_history;
    delete[] wfh;
    delete table;
    return 0;
}
