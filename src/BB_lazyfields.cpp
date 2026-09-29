// OWNERSHIP=Claude
#ifndef BB_LAZYFIELDS_CPP
#define BB_LAZYFIELDS_CPP
#include "../lib/BB_lazyfields.hpp"

// The "expensive" BB_lazyfields getters - declared in lib/Bitboards.hpp, defined here
// because they need attacked_squares()/get_bishop_attacks()/get_rook_attacks() (src/magics.cpp),
// in_check() (src/checks.cpp) and count_legal_moves() (src/move_generation.cpp), none of which exist yet
// at the point Bitboards.hpp is first included.

uint64_t BB_lazyfields::get_attacked_squares(const BB* const owner, bool by_white)
{
    if(by_white)
    {
        if(!white_attacks_valid)
        {
            white_attacks = attacked_squares(owner->Board, true);
            white_attacks_valid = true;
        }
        return white_attacks;
    }
    if(!black_attacks_valid)
    {
        black_attacks = attacked_squares(owner->Board, false);
        black_attacks_valid = true;
    }
    return black_attacks;
}

bool BB_lazyfields::get_in_check(const BB* const owner)
{
    if(in_check_cache == -1)
    in_check_cache = in_check(owner->Board, owner->white_move) ? 1 : 0;
    return in_check_cache == 1;
}

bool BB_lazyfields::get_has_legal_move(const BB* const owner)
{
    // count_legal_moves() stops at the first legal move and tests legality by masks;
    // one_move() heap-allocated a BB and made each candidate move to call in_check().
    if(has_legal_move_cache == -1)
    has_legal_move_cache = count_legal_moves(owner, 1) ? 1 : 0;
    return has_legal_move_cache == 1;
}

uint64_t BB::get_attacked_squares(bool by_white) const { return lazy.get_attacked_squares(this, by_white); }
bool BB::get_in_check() const { return lazy.get_in_check(this); }
bool BB::get_has_legal_move() const { return lazy.get_has_legal_move(this); }

#endif
