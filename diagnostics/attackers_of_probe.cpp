// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"
#include "../src/see.cpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void expect(bool condition, const std::string& message)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

static void setup()
{
    initialize_rand();
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
}

static int square(char file, int rank) // file 'a'..'h', rank 1..8
{
    return (file - 'a') + 8 * (rank - 1);
}

int main()
{
    setup();

    // Rook d2 attacks knight on d5 along an open file; no black attacker of d5.
    {
        BB parent;
        FEN_to_BB("4k3/8/8/3n4/8/8/3R4/4K3 w - - 0 1", &parent);
        castling_rights(&parent);

        uint64_t occ = parent.Board[0]|parent.Board[1]|parent.Board[2]|parent.Board[3]
                     | parent.Board[4]|parent.Board[5]|parent.Board[6]|parent.Board[7]
                     | parent.Board[8]|parent.Board[9]|parent.Board[10]|parent.Board[11];
        AttackersOfSquare att = attackers_of(square('d',5), occ, parent.Board);
        expect(att.white == (1ULL << square('d',2)), "expected only Rd2 to attack d5");
        expect(att.black == 0, "expected no black attacker of d5");
    }

    // Rook battery on the d-file: with full occupancy only the near rook (d3) shows
    // up as an attacker of d5; the far rook (d1) is an x-ray hidden behind it.
    // Removing d3 from the occupancy must reveal d1.
    {
        BB parent;
        FEN_to_BB("3rk3/8/8/3n4/8/3R4/8/3RK3 w - - 0 1", &parent);
        castling_rights(&parent);

        uint64_t occ = parent.Board[0]|parent.Board[1]|parent.Board[2]|parent.Board[3]
                     | parent.Board[4]|parent.Board[5]|parent.Board[6]|parent.Board[7]
                     | parent.Board[8]|parent.Board[9]|parent.Board[10]|parent.Board[11];

        AttackersOfSquare full = attackers_of(square('d',5), occ, parent.Board);
        expect(full.white == (1ULL << square('d',3)), "expected only Rd3 visible with full occupancy");
        expect(full.black == (1ULL << square('d',8)), "expected Rd8 to attack d5");

        uint64_t occ_after_d3_gone = occ & ~(1ULL << square('d',3));
        AttackersOfSquare xray = attackers_of(square('d',5), occ_after_d3_gone, parent.Board);
        expect(xray.white == (1ULL << square('d',1)), "expected Rd1 x-ray revealed once Rd3 leaves");
        expect(xray.black == (1ULL << square('d',8)), "black attacker set should be unaffected");
    }

    std::cout << "attackers_of_probe passed\n";
    return 0;
}
