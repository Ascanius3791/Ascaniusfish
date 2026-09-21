// OWNERSHIP=Claude
#include <cstdint>
#include <iostream>

bool actual_code_logic(uint64_t occupancy, int i) {
    return !((occupancy & (1ULL << i >> 8)));
}

bool intended_logic(uint64_t occupancy, int i) {
    return !((occupancy & (1ULL << (i - 8))));
}

int main() {
    bool ok = true;
    for (int i = 8; i < 64; ++i) {
        uint64_t occupancy = 1ULL << (i - 8);
        if (actual_code_logic(occupancy, i) != intended_logic(occupancy, i)) {
            std::cout << "Mismatch at i=" << i << "\n";
            ok = false;
            break;
        }
    }

    if (ok) {
        std::cout << "All legal black-pawn indices (8..63) match the intended i-8 check.\n";
    } else {
        std::cout << "Mismatch found.\n";
        return 1;
    }

    std::cout << "For a legal black pawn on square 48 (rank 7 -> 6), the expression evaluates as: "
              << actual_code_logic((1ULL << 40), 48) << "\n";
    std::cout << "Expected for occupancy at 40: " << intended_logic((1ULL << 40), 48) << "\n";

    // demonstrates the false-positive concern is only for invalid, non-pawn indices
    if (actual_code_logic(1ULL << 0, 7) != intended_logic(1ULL << 0, 7)) {
        std::cout << "The expression differs for invalid index 7; that's not a legal black-pawn square.\n";
    } else {
        std::cout << "Index 7 is not a legal black pawn forward square, and the expression still happens to agree there.\n";
    }

    return 0;
}
