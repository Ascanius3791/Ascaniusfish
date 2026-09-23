// OWNERSHIP=Claude
// Ad-hoc probe: walk a PV line move-by-move via legal move generation (to verify
// each step is actually legal), then dissect basic_eval() into its components
// for the resulting position.
#include <iostream>
#include "../ascaniusfish.hpp"

bool same_pieces(const BB& a, const BB& b) {
    for (int i = 0; i < 12; i++) if (a.Board[i] != b.Board[i]) return false;
    return true;
}

bool step_to(BB& pos, const BB& target_pieces, const char* label) {
    BB* wfh = new BB[300];
    auto result = all_moves(&pos, wfh);
    int n = std::get<0>(result);
    for (int i = 0; i < n; i++) {
        if (same_pieces(wfh[i], target_pieces)) {
            pos = wfh[i];
            delete[] wfh;
            std::cout << "step '" << label << "': OK (legal move found)\n";
            return true;
        }
    }
    delete[] wfh;
    std::cout << "step '" << label << "': FAILED - no legal move reaches that placement\n";
    return false;
}

int main() {
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    BB pos("rnbqkb1r/pp1pn2p/2p1p1p1/5p2/2PPP3/2NB1N2/PP3PPP/R1BQK2R b KQkq - 11 12");

    const char* boards[] = {
        "rnbqkb1r/pp2n2p/2ppp1p1/5p2/2PPP3/2NB1N2/PP3PPP/R1BQK2R",   // 1...d6
        "rnbqkb1r/pp2n2p/2ppp1p1/4Pp2/2PP4/2NB1N2/PP3PPP/R1BQK2R",   // 2.e5
        "rnbqkb1r/p3n2p/1pppp1p1/4Pp2/2PP4/2NB1N2/PP3PPP/R1BQK2R",   // 2...b6
        "rnbqkb1r/p3n2p/1pppp1p1/4Pp2/2PP1B2/2NB1N2/PP3PPP/R2QK2R",  // 3.Bf4 (Bc1-f4)
        "rnbqkb1r/p3n2p/1pp1p1p1/3pPp2/2PP1B2/2NB1N2/PP3PPP/R2QK2R", // 3...d5
    };
    const char* labels[] = {"1...d6", "2.e5", "2...b6", "3.Bf4", "3...d5"};

    bool all_ok = true;
    for (int i = 0; i < 5; i++) {
        BB target(std::string(boards[i]) + " w - - 0 1");
        all_ok &= step_to(pos, target, labels[i]);
    }
    if (!all_ok) {
        std::cout << "\nOne or more steps were not legal moves from the previous position -- "
                      "hand-derived line is wrong, aborting eval breakdown.\n";
        return 1;
    }

    WEIGHTS W = WEIGHTS_OG;
    int mat = material_eval(&pos, W);
    int pt = piecetable(&pos, W);
    int ks_w = king_safety_of_colour(pos.Board, true, W);
    int ks_b = king_safety_of_colour(pos.Board, false, W);
    int posi = positional_eval(&pos, W);
    int pawn_w = pawn_struckture_eval_of_colour(&pos, true, W);
    int pawn_b = pawn_struckture_eval_of_colour(&pos, false, W);
    int central_w = central_pawn_presence(&pos, true, W);
    int central_b = central_pawn_presence(&pos, false, W);
    int mob = 5 * (count(attacks_by_col(pos.Board, 1)) - count(attacks_by_col(pos.Board, 0)));
    int activity = piece_activity_eval(&pos, W);

    int total = mat + pt + ks_w - ks_b + posi + mob + activity;
    int full_eval = eval(&pos, W, -1);

    std::cout << "\nFinal position (white_move=" << pos.white_move << ")\n\n";
    std::cout << "material_eval:                " << mat << "\n";
    std::cout << "piecetable:                    " << pt << "\n";
    std::cout << "king_safety (white):           " << ks_w << "\n";
    std::cout << "king_safety (black):           " << ks_b << "\n";
    std::cout << "  -> king_safety contribution: " << (ks_w - ks_b) << "\n";
    std::cout << "positional_eval (pawn struct): " << posi << "\n";
    std::cout << "  pawn_struct white:            " << pawn_w << " (central_pawn_presence=" << central_w << ")\n";
    std::cout << "  pawn_struct black:            " << pawn_b << " (central_pawn_presence=" << central_b << ")\n";
    std::cout << "mobility (5*attack diff):      " << mob << "\n";
    std::cout << "piece_activity_eval:           " << activity << "\n";
    std::cout << "---------------------------------\n";
    std::cout << "sum of components:             " << total << "\n";
    std::cout << "eval() (with exception check):  " << full_eval << "\n";

    return 0;
}
