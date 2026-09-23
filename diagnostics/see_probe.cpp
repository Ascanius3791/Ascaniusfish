// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"
#include "../src/see.cpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

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

    // (a) Simple undefended capture: Rd2xNd5. Captured(N)=300 < moving(R)=500, so
    // Tier 1 doesn't fire; d5 is undefended (only the far-away black king) so Tier 2
    // returns the captured piece's value directly.
    {
        BB parent;
        FEN_to_BB("4k3/8/8/3n4/8/8/3R4/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('d',2), to = square('d',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false, WEIGHTS_OG);
        expect(see == 300, "(a) expected SEE == 300, got " + std::to_string(see));
        expect(is_good_capture(&parent, from, to, false, WEIGHTS_OG), "(a) expected a good capture");
    }

    // (b) Losing exchange behind a single defender: Qd2xPd5, defended by the black
    // pawn on e6. gain=[100,800] folds to -800: queen wins a pawn then is recaptured.
    {
        BB parent;
        FEN_to_BB("4k3/8/4p3/3p4/8/8/3Q4/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('d',2), to = square('d',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false, WEIGHTS_OG);
        expect(see == -800, "(b) expected SEE == -800, got " + std::to_string(see));
        expect(!is_good_capture(&parent, from, to, false, WEIGHTS_OG), "(b) expected a losing capture");
    }

    // (c) X-ray revealed by the mover's own departure: Rd3xNd5. White's second rook
    // on d1 is invisible until Rd3 vacates d3; black's Rd8 recaptures, then the
    // x-rayed Rd1 recaptures. gain=[300,200,300] folds to +300.
    {
        BB parent;
        FEN_to_BB("3rk3/8/8/3n4/8/3R4/8/3RK3 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('d',3), to = square('d',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false, WEIGHTS_OG);
        expect(see == 300, "(c) expected SEE == 300, got " + std::to_string(see));
        expect(is_good_capture(&parent, from, to, false, WEIGHTS_OG), "(c) expected a good capture");
    }

    // (d) Least-valuable-attacker-first ordering matters: Nf3xPe5. Black can
    // recapture with either Nc6(300) or Qe8(900) - correct play uses the knight
    // first. gain=[100,200,100,400] folds to -200. Recapturing with the queen first
    // (a plausible bug) would instead fold to +100, the opposite sign.
    {
        BB parent;
        FEN_to_BB("4q1k1/8/2n5/4p3/8/5N2/8/4R1K1 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('f',3), to = square('e',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false, WEIGHTS_OG);
        expect(see == -200, "(d) expected SEE == -200, got " + std::to_string(see));
        expect(!is_good_capture(&parent, from, to, false, WEIGHTS_OG), "(d) expected a losing capture");
    }

    // (e) En passant: pawn value == pawn value, so Tier 1 always resolves this as a
    // wash (0) regardless of who's defending - the interesting part is that the
    // captured pawn is correctly located behind the destination square. Exercised
    // through real move generation and both wrapper functions, not hand-picked
    // squares, so a wrong captured-square offset would show up as "not a capture"
    // rather than silently agreeing.
    {
        BB parent;
        FEN_to_BB("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", &parent);
        castling_rights(&parent);

        BB children[64];
        auto generated = all_moves(&parent, children, 64);
        int number_of_moves = std::get<0>(generated);
        std::vector<Move> moves = std::get<1>(generated);

        int ep_index = -1;
        for(int i=0;i<number_of_moves;i++)
            if(moves[i].is_en_passant) { ep_index = i; break; }
        expect(ep_index != -1, "(e) expected an en passant move to be generated");

        const Move& move = moves[ep_index];
        expect(is_capturing_move(parent.Board, move.to, parent.white_move, true),
               "(e) en passant must be treated as a capture");
        int see = static_exchange_eval(parent.Board, move.from, move.to, parent.white_move, true, WEIGHTS_OG);
        expect(see == 0, "(e) expected SEE == 0 for a pawn-for-pawn en passant, got " + std::to_string(see));
        expect(is_good_capture(&parent, move.from, move.to, true, WEIGHTS_OG), "(e) expected a good (wash) capture");
        expect(is_good_capture_from_child(&parent, children+ep_index, WEIGHTS_OG),
               "(e) is_good_capture_from_child should agree, reconstructing en passant from the board diff");
    }

    std::cout << "see_probe passed\n";
    return 0;
}
