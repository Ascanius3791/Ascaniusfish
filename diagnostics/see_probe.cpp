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

    // (a) Simple undefended capture: Rd2xNd5. d5 is undefended (the black king
    // is too far away), so SEE is the knight's value.
    {
        BB parent;
        FEN_to_BB("4k3/8/8/3n4/8/8/3R4/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('d',2), to = square('d',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false);
        expect(see == 3, "(a) expected SEE == 3, got " + std::to_string(see));
        expect(is_good_capture(&parent, from, to, false), "(a) expected a good capture");
    }

    // (b) Losing exchange behind a single defender: Qd2xPd5, defended by the black
    // pawn on e6. gain=[1,8] folds to -8: queen wins a pawn then is recaptured.
    {
        BB parent;
        FEN_to_BB("4k3/8/4p3/3p4/8/8/3Q4/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('d',2), to = square('d',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false);
        expect(see == -8, "(b) expected SEE == -8, got " + std::to_string(see));
        expect(!is_good_capture(&parent, from, to, false), "(b) expected a losing capture");
    }

    // (c) X-ray revealed by the mover's own departure: Rd3xNd5. White's second rook
    // on d1 is invisible until Rd3 vacates d3; black's Rd8 recaptures, then the
    // x-rayed Rd1 recaptures. gain=[3,2,3] folds to +3.
    {
        BB parent;
        FEN_to_BB("3rk3/8/8/3n4/8/3R4/8/3RK3 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('d',3), to = square('d',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false);
        expect(see == 3, "(c) expected SEE == 3, got " + std::to_string(see));
        expect(is_good_capture(&parent, from, to, false), "(c) expected a good capture");
    }

    // (d) Least-valuable-attacker-first ordering matters: Nf3xPe5. Black can
    // recapture with either Nc6(3) or Qe8(9) - correct play uses the knight
    // first. gain=[1,2,1,4] folds to -2. Recapturing with the queen first
    // (a plausible bug) would instead fold to +1, the opposite sign.
    {
        BB parent;
        FEN_to_BB("4q1k1/8/2n5/4p3/8/5N2/8/4R1K1 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('f',3), to = square('e',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false);
        expect(see == -2, "(d) expected SEE == -2, got " + std::to_string(see));
        expect(!is_good_capture(&parent, from, to, false), "(d) expected a losing capture");
    }

    // (e) En passant: d6 is undefended, so SEE wins the pawn (+1) - the
    // interesting part is that the captured pawn is correctly located behind the
    // destination square. Exercised
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
        int see = static_exchange_eval(parent.Board, move.from, move.to, parent.white_move, true);
        expect(see == 1, "(e) expected SEE == 1 for a free en passant pawn, got " + std::to_string(see));
        expect(is_good_capture(&parent, move.from, move.to, true), "(e) expected a good capture");
        expect(is_good_capture_from_child(&parent, children+ep_index),
               "(e) is_good_capture_from_child should agree, reconstructing en passant from the board diff");
    }

    // (f) The king recaptures only as the last piece: Qd2xPd5 with d5 defended
    // by the black king alone loses the queen (gain=[1,9] folds to -8); add a
    // white rook on d1 behind the queen and the king can't take back (+1).
    {
        BB parent;
        FEN_to_BB("8/8/4k3/3p4/8/8/3Q4/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        int from = square('d',2), to = square('d',5);
        int see = static_exchange_eval(parent.Board, from, to, parent.white_move, false);
        expect(see == -8, "(f) expected SEE == -8 with only the king defending, got " + std::to_string(see));
        FEN_to_BB("8/8/4k3/3p4/8/8/3Q4/3RK3 w - - 0 1", &parent);
        castling_rights(&parent);
        see = static_exchange_eval(parent.Board, from, to, parent.white_move, false);
        expect(see == 1, "(f) expected SEE == 1 with the x-ray rook behind, got " + std::to_string(see));
    }

    // (g) Promotion gain: b7xa8=Q wins the rook plus 8 (+13) when a8 is free;
    // a quiet b8=Q under the Rh8's attack is recaptured: gain=[8,9] folds to -1.
    {
        BB parent;
        FEN_to_BB("r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        int see = static_exchange_eval(parent.Board, square('b',7), square('a',8), parent.white_move, false, QUEEN_PROMOTION);
        expect(see == 13, "(g) expected SEE == 13 for bxa8=Q, got " + std::to_string(see));
        FEN_to_BB("7r/1P6/8/8/8/8/k7/4K3 w - - 0 1", &parent);
        castling_rights(&parent);
        see = static_exchange_eval(parent.Board, square('b',7), square('b',8), parent.white_move, false, QUEEN_PROMOTION);
        expect(see == -1, "(g) expected SEE == -1 for a hanging b8=Q, got " + std::to_string(see));
    }

    std::cout << "see_probe passed\n";
    return 0;
}
