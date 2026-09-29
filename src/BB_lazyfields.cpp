// OWNERSHIP=Claude
#ifndef BB_LAZYFIELDS_CPP
#define BB_LAZYFIELDS_CPP
#include "../lib/BB_lazyfields.hpp"

// The "expensive" BB_lazyfields getters - declared in lib/Bitboards.hpp, defined here
// because they need attacked_squares()/get_bishop_attacks()/get_rook_attacks() (src/magics.cpp),
// the leaper templates, between_squares and count_legal_moves() (src/move_generation.cpp), none of which exist yet
// at the point Bitboards.hpp is first included.

uint64_t BB_lazyfields::get_attacked_squares(const BB* const owner, bool by_white)
{
    if(by_white)
    {
        if(!(valid & WHITE_ATTACKS))
        {
            white_attacks = attacked_squares(owner->Board, true);
            valid |= WHITE_ATTACKS;
        }
        return white_attacks;
    }
    if(!(valid & BLACK_ATTACKS))
    {
        black_attacks = attacked_squares(owner->Board, false);
        valid |= BLACK_ATTACKS;
    }
    return black_attacks;
}

// Checkers and pins come from the same king square and enemy sliders, so one pass
// fills both. An enemy slider that sees the king through own pieces pins the one own
// piece between them, if there is exactly one.
void BB_lazyfields::fill_checkers_and_pinned(const BB* const owner)
{
    const bool WM = owner->white_move;
    const int OWN = 6*!WM, ENE = 6*WM;
    const uint64_t* const B = owner->Board;
    const uint64_t own = get_pieces_of_colour(owner, WM);
    const uint64_t enemy = get_pieces_of_colour(owner, !WM);
    const uint64_t occ = get_occupancy(owner);
    const int k = __builtin_ctzll(B[5+OWN]);
    const uint64_t enemy_orth = B[1+ENE]|B[4+ENE];
    const uint64_t enemy_diag = B[3+ENE]|B[4+ENE];

    checkers = ((WM ? BP_template[k] : WP_template[k]) & B[0+ENE])
             | (Kn_template[k] & B[2+ENE])
             | (K_template[k] & B[5+ENE])
             | (get_rook_attacks(k,occ) & enemy_orth)
             | (get_bishop_attacks(k,occ) & enemy_diag);

    pinned = 0;
    uint64_t pinners = (get_rook_attacks(k,enemy) & enemy_orth)
                     | (get_bishop_attacks(k,enemy) & enemy_diag);
    while(pinners)
    {
        const uint64_t blockers = between_squares.sq[k][find_and_delete_trailling_1(pinners)] & own;
        if(blockers && !(blockers & (blockers-1)))
        pinned |= blockers;
    }
    valid |= CHECKERS_AND_PINNED;
}

uint64_t BB_lazyfields::get_checkers(const BB* const owner)
{
    if(!(valid & CHECKERS_AND_PINNED))
    fill_checkers_and_pinned(owner);
    return checkers;
}

uint64_t BB_lazyfields::get_pinned(const BB* const owner)
{
    if(!(valid & CHECKERS_AND_PINNED))
    fill_checkers_and_pinned(owner);
    return pinned;
}

bool BB_lazyfields::get_in_check(const BB* const owner)
{
    return get_checkers(owner) != 0;
}

bool BB_lazyfields::get_has_legal_move(const BB* const owner)
{
    // count_legal_moves() stops at the first legal move and tests legality by masks;
    // one_move() heap-allocated a BB and made each candidate move to call in_check().
    if(!(valid & HAS_LEGAL_MOVE))
    {
        has_legal_move = count_legal_moves(owner, 1) != 0;
        valid |= HAS_LEGAL_MOVE;
    }
    return has_legal_move;
}

uint64_t BB::get_attacked_squares(bool by_white) const { return lazy.get_attacked_squares(this, by_white); }
uint64_t BB::get_checkers() const { return lazy.get_checkers(this); }
uint64_t BB::get_pinned() const { return lazy.get_pinned(this); }
bool BB::get_in_check() const { return lazy.get_in_check(this); }
bool BB::get_has_legal_move() const { return lazy.get_has_legal_move(this); }

#endif
