// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"

#include <iostream>
#include <string>

static void setup()
{
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
}

static void check_position(const std::string& label, const std::string& fen)
{
    BB parent;
    FEN_to_BB(fen,&parent);
    castling_rights(&parent);

    BB children[128];
    const int number_of_moves=all_moves(&parent,children,127);
    const std::vector<int> assigned_depth=assign_depth(&parent,children,number_of_moves,10,WEIGHTS_OG);

    std::cout << label << '\n';
    std::cout << "fen: " << fen << '\n';
    std::cout << "moves: " << number_of_moves << '\n';
    for(int i=0;i<number_of_moves;i++)
    {
        const bool winning_capture=captures_more_valuable_piece(&parent,children+i,WEIGHTS_OG);
        if(winning_capture || assigned_depth[i]==INT_MAX)
        {
            std::cout << get_move(&parent,children+i)
                      << " winning_capture=" << winning_capture
                      << " assigned_depth="
                      << (assigned_depth[i]==INT_MAX ? std::string("INT_MAX") : std::to_string(assigned_depth[i]))
                      << " resolved=" << resolved_assigned_depth(assigned_depth[i],10)
                      << '\n';
        }
    }
    std::cout << '\n';
}

int main()
{
    setup();

    check_position(
        "pawn can capture queen",
        "4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1");
    check_position(
        "queen can capture pawn",
        "4k3/8/8/3p4/4Q3/8/8/4K3 w - - 0 1");
    check_position(
        "rook can capture rook",
        "4k3/8/8/3r4/4R3/8/8/4K3 w - - 0 1");
    check_position(
        "knight can capture rook",
        "4k3/8/5r2/8/4N3/8/8/4K3 w - - 0 1");

    return 0;
}
