// OWNERSHIP=Claude
// UCI entry point: ./ascaniusfish_uci speaks UCI on stdin/stdout, for GUIs,
// match runners and other tools. The interactive play mode stays in
// ./ascaniusfish (ascaniusfish.cpp).
#include "lib/uci.hpp"

int main()
{
    Zobrist zobrist_keys;
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);//bishop
    init_sliders_attacks(0);//rook
    return uci_loop();
}
