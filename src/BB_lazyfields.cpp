// OWNERSHIP=Claude
#ifndef BB_LAZYFIELDS_CPP
#define BB_LAZYFIELDS_CPP
#include "../lib/BB_lazyfields.hpp"

// The "expensive" BB_lazyfields getters - declared in lib/Bitboards.hpp, defined here
// because they need attacked_squares()/get_bishop_attacks()/get_rook_attacks() (src/magics.cpp),
// in_check() (src/checks.cpp) and one_move() (src/move_generation.cpp), none of which exist yet
// at the point Bitboards.hpp is first included.

uint64_t get_attacked_squares(const BB* const b, bool by_white)
{
    if(by_white)
    {
        if(!b->lazy.white_attacks_valid)
        {
            b->lazy.white_attacks = attacked_squares(b->Board, true);
            b->lazy.white_attacks_valid = true;
        }
        return b->lazy.white_attacks;
    }
    if(!b->lazy.black_attacks_valid)
    {
        b->lazy.black_attacks = attacked_squares(b->Board, false);
        b->lazy.black_attacks_valid = true;
    }
    return b->lazy.black_attacks;
}

bool get_in_check(const BB* const b)
{
    if(b->lazy.in_check_cache == -1)
    b->lazy.in_check_cache = in_check(b->Board, b->white_move) ? 1 : 0;
    return b->lazy.in_check_cache == 1;
}

bool get_has_legal_move(const BB* const b)
{
    if(b->lazy.has_legal_move_cache == -1)
    b->lazy.has_legal_move_cache = one_move(b) ? 1 : 0;
    return b->lazy.has_legal_move_cache == 1;
}

#endif
