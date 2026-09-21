// OWNERSHIP=Claude
#include "../ascaniusfish.hpp"
#include "../ascaniusfish_2.hpp"
#include "../lib/cuckoo_cycle_table.hpp"

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

int main()
{
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);
    Zobrist zobrist_init = Zobrist(); // populates Zobrist::pieceKeys/sideKey/etc (see src/zobrist.cpp)

    CuckooCycleTable table;
    std::cout << "CuckooCycleTable: " << table.entries_stored() << " / " << table.size() << " slots used." << std::endl;
    expect(table.entries_stored()>0, "table should have inserted at least some entries");
    expect(table.entries_stored() < table.size()/2, "load factor should stay comfortably under 50% for reliable cuckoo hashing");

    // A known reversible rook move: from square 0, a rook can always reach
    // somewhere on an empty board (get_rook_attacks(0,0) is never zero).
    {
        int from = 0;
        int piece_type = 1; // white rook, see BB::Board's piece-index convention
        uint64_t reachable = get_rook_attacks(from, 0ULL);
        expect(reachable!=0, "sanity: a rook on an empty board must be able to reach somewhere from square 0");
        int to = find_and_delete_trailling_1(reachable);

        uint64_t delta = Zobrist::pieceKeys[piece_type][from] ^ Zobrist::pieceKeys[piece_type][to];
        int found_piece_type;
        Move found_move;
        bool found = table.probe(delta, found_piece_type, found_move);
        expect(found, "probe should find the delta for a real rook move");
        expect(found_piece_type==piece_type, "probe should report the correct piece type");
        bool matches_either_direction = (found_move==Move(from,to)) || (found_move==Move(to,from));
        expect(matches_either_direction, "probe should return a move matching this square pair (either direction, since the delta is symmetric)");
    }

    // A black knight move, to confirm color offset (+6) is handled.
    {
        int from = 27; // an interior square with several knight destinations
        int piece_type = 8; // black knight (2 + 6*1)
        uint64_t reachable = Kn_template[from];
        expect(reachable!=0, "sanity: a knight on square 27 must have some destinations");
        int to = find_and_delete_trailling_1(reachable);

        uint64_t delta = Zobrist::pieceKeys[piece_type][from] ^ Zobrist::pieceKeys[piece_type][to];
        int found_piece_type;
        Move found_move;
        bool found = table.probe(delta, found_piece_type, found_move);
        expect(found, "probe should find the delta for a real black knight move");
        expect(found_piece_type==piece_type, "probe should report black knight's piece type, not white's");
    }

    // A delta that doesn't correspond to any real move should not be found
    // (astronomically unlikely to coincidentally collide with a real one).
    {
        int found_piece_type;
        Move found_move;
        bool found = table.probe(0xDEADBEEFCAFEF00DULL, found_piece_type, found_move);
        expect(!found, "an arbitrary delta should not match any real move");
    }

    // Pawns must never appear - pawn moves are irreversible so they can't be
    // part of a repetition cycle.
    {
        for(int piece_type : {0, 6}) // white pawn, black pawn
        {
            for(int from=0; from<64; from++)
            for(int to=0; to<64; to++)
            {
                if(from==to) continue;
                uint64_t delta = Zobrist::pieceKeys[piece_type][from] ^ Zobrist::pieceKeys[piece_type][to];
                int found_piece_type;
                Move found_move;
                if(table.probe(delta, found_piece_type, found_move))
                {
                    expect(found_piece_type!=0 && found_piece_type!=6, "no pawn delta should ever be found in the table (this would be a real hash collision, not expected)");
                }
            }
        }
    }

    // --- probe_with_ply_gap / probe_with_board_check: real two-position setup ---
    // BB() default-constructs to a completely empty board (no pieces, white to
    // move, all castling rights true, no en passant) - place one white knight on
    // it ourselves so we have two real, fully consistent positions one quiet
    // move apart, without needing to know this engine's algebraic square mapping.
    {
        BB board_a = BB();
        int knight_from = 10;
        uint64_t reachable = Kn_template[knight_from];
        expect(reachable!=0, "sanity: square 10 must have some knight destinations");
        int knight_to = find_and_delete_trailling_1(reachable);

        board_a.Board[2] = 1ULL << knight_from; // white knight
        board_a.zobrist_hash = Zobrist::compute_Zobrist_Hash(board_a);

        BB board_b = board_a;
        board_b.Board[2] = 1ULL << knight_to;
        board_b.white_move = !board_a.white_move; // one real move flips side to move
        board_b.zobrist_hash = Zobrist::compute_Zobrist_Hash(board_b);

        // Correct parity (1 real ply apart) should find it.
        {
            int found_piece_type;
            Move found_move;
            bool found = table.probe_with_ply_gap(board_a.zobrist_hash, board_b.zobrist_hash, 1, found_piece_type, found_move);
            expect(found, "probe_with_ply_gap: two positions genuinely one move apart should be found at ply_gap=1");
            expect(found_piece_type==2, "probe_with_ply_gap: should report white knight");
        }

        // Wrong parity should NOT find it - this is exactly the bug that would
        // exist without accounting for the side-to-move key at all.
        {
            int found_piece_type;
            Move found_move;
            bool found = table.probe_with_ply_gap(board_a.zobrist_hash, board_b.zobrist_hash, 2, found_piece_type, found_move);
            expect(!found, "probe_with_ply_gap: the SAME pair of hashes must not match at the wrong ply_gap parity");
        }

        // probe_with_board_check against board_a: the piece is on `from` there,
        // so the returned move should be oriented from->to.
        {
            int found_piece_type;
            Move found_move;
            bool found = table.probe_with_board_check(board_a.zobrist_hash, board_b.zobrist_hash, 1, board_a, found_piece_type, found_move);
            expect(found, "probe_with_board_check: should find and validate the move against board_a");
            expect(found_move==Move(knight_from, knight_to), "probe_with_board_check: oriented from board_a, move should read from->to");
        }

        // probe_with_board_check against board_b: the piece is on `to` there, so
        // the move should come back oriented to->from (canonicalized to wherever
        // the piece actually is in the board we handed it).
        {
            int found_piece_type;
            Move found_move;
            bool found = table.probe_with_board_check(board_a.zobrist_hash, board_b.zobrist_hash, 1, board_b, found_piece_type, found_move);
            expect(found, "probe_with_board_check: should find and validate the move against board_b");
            expect(found_move==Move(knight_to, knight_from), "probe_with_board_check: oriented from board_b, move should read to->from");
        }

        // False-positive rejection: the hashes/ply_gap still describe a real
        // move-shaped delta, but if we hand it a board where that knight is
        // sitting somewhere else entirely, the board-check must reject it even
        // though probe_with_ply_gap alone still finds the (now-irrelevant) match.
        {
            BB unrelated_board = board_a;
            uint64_t other_reachable = Kn_template[knight_to] & ~(1ULL << knight_from); // exclude knight_from itself
            expect(other_reachable!=0, "test setup: square knight_to needs a reachable square other than knight_from");
            int elsewhere = find_and_delete_trailling_1(other_reachable); // some third square
            unrelated_board.Board[2] = 1ULL << elsewhere;

            int found_piece_type;
            Move found_move;
            bool raw_found = table.probe_with_ply_gap(board_a.zobrist_hash, board_b.zobrist_hash, 1, found_piece_type, found_move);
            expect(raw_found, "sanity: the underlying delta still matches without a board check");

            bool checked_found = table.probe_with_board_check(board_a.zobrist_hash, board_b.zobrist_hash, 1, unrelated_board, found_piece_type, found_move);
            expect(!checked_found, "probe_with_board_check: must reject a match when the piece isn't actually on either endpoint square");
        }

        // probe_full: identical castling/en-passant at both endpoints (board_a
        // and board_b only differ in the knight's square and side to move,
        // exactly as set up above) - should find the same match as
        // probe_with_board_check(board_a) did.
        {
            int found_piece_type;
            Move found_move;
            bool found = table.probe_full(board_a, board_b, 1, found_piece_type, found_move);
            expect(found, "probe_full: should find the move when castling/en-passant are identical at both endpoints");
            expect(found_move==Move(knight_from, knight_to), "probe_full: should orient the move from board_a's perspective");
        }

        // probe_full: differing castling rights must be rejected outright, even
        // though the piece-square delta (and everything probe_with_board_check
        // checks) is otherwise identical to the passing case above.
        {
            BB board_b_diff_castling = board_b;
            board_b_diff_castling.castle[0][0] = !board_b_diff_castling.castle[0][0];

            int found_piece_type;
            Move found_move;
            bool found = table.probe_full(board_a, board_b_diff_castling, 1, found_piece_type, found_move);
            expect(!found, "probe_full: must reject a match when castling rights differ between the two boards");
        }

        // probe_full: differing en-passant availability must also be rejected.
        {
            BB board_b_diff_ep = board_b;
            board_b_diff_ep.en_passant = 1ULL << 40; // some arbitrary square, board_a has none

            int found_piece_type;
            Move found_move;
            bool found = table.probe_full(board_a, board_b_diff_ep, 1, found_piece_type, found_move);
            expect(!found, "probe_full: must reject a match when en-passant availability differs between the two boards");
        }
    }

    // --- verify_move_is_legal_now / detect_upcoming_cycle ---
    // These need real board occupancy, so squares are found by SEARCHING with
    // the same attack-table queries the code under test uses, rather than
    // hardcoded algebraic squares (this file doesn't assume any particular
    // square-index<->algebraic mapping).
    {
        // Reuse the same knight pair as before: board_a (knight on knight_from)
        // and board_b (knight on knight_to, one ply later) are a genuine
        // one-move-apart pair with identical castling/en-passant.
        BB board_a = BB();
        int knight_from = 10;
        uint64_t reachable = Kn_template[knight_from];
        int knight_to = find_and_delete_trailling_1(reachable);
        board_a.Board[2] = 1ULL << knight_from;
        board_a.zobrist_hash = Zobrist::compute_Zobrist_Hash(board_a);

        BB board_b = board_a;
        board_b.Board[2] = 1ULL << knight_to;
        board_b.white_move = !board_a.white_move;
        board_b.zobrist_hash = Zobrist::compute_Zobrist_Hash(board_b);

        // detect_upcoming_cycle from board_b's perspective: it's one move away
        // from recreating board_a (moving the knight back). Nothing else is on
        // the board, so this move is trivially legal (empty destination, no
        // blockers, no king anywhere to put in check... but verify_move_is_legal_now
        // still needs a king to exist for in_check() to behave sensibly, so add
        // kings for both sides, far from the knight's path).
        int white_king_sq = 0;
        while((Kn_template[knight_from] | Kn_template[knight_to] | (1ULL<<knight_from) | (1ULL<<knight_to)) & (1ULL<<white_king_sq))
        white_king_sq++;
        int black_king_sq = 63;
        while(((Kn_template[knight_from] | Kn_template[knight_to] | (1ULL<<knight_from) | (1ULL<<knight_to) | (1ULL<<white_king_sq))) & (1ULL<<black_king_sq))
        black_king_sq--;

        board_a.Board[5] = 1ULL << white_king_sq;   // white king
        board_a.Board[11] = 1ULL << black_king_sq;  // black king
        board_a.zobrist_hash = Zobrist::compute_Zobrist_Hash(board_a);
        board_b.Board[5] = 1ULL << white_king_sq;
        board_b.Board[11] = 1ULL << black_king_sq;
        board_b.zobrist_hash = Zobrist::compute_Zobrist_Hash(board_b);

        {
            int found_piece_type;
            Move found_move;
            bool found = table.detect_upcoming_cycle(board_b, board_a, 1, found_piece_type, found_move);
            expect(found, "detect_upcoming_cycle: a genuinely legal reversible move back to an ancestor should be found");
            expect(found_move==Move(knight_to, knight_from), "detect_upcoming_cycle: move should be oriented from board_b's own piece placement");
        }

        // Occupied destination: put an unrelated piece (a pawn - irrelevant to
        // the move being tested) on knight_from, so board_b's knight "moving
        // back" would land on an occupied square.
        {
            BB board_b_blocked = board_b;
            board_b_blocked.Board[0] |= (1ULL << knight_from); // white pawn, occupies the destination
            int found_piece_type;
            Move found_move;
            bool found = table.detect_upcoming_cycle(board_b_blocked, board_a, 1, found_piece_type, found_move);
            expect(!found, "detect_upcoming_cycle: must reject when the destination square is occupied");
        }
    }

    // Blocked sliding path: find a rook move that only works on an empty
    // board, then confirm a real blocker on the ray makes verify_move_is_legal_now
    // reject it (even though probe()/probe_full() still "match" the delta,
    // since they don't know about real occupancy).
    {
        int rook_from = 0;
        int rook_to = -1, blocker_square = -1;
        uint64_t empty_board_targets = get_rook_attacks(rook_from, 0ULL);
        uint64_t targets_copy = empty_board_targets;
        while(targets_copy && rook_to==-1)
        {
            int candidate_to = find_and_delete_trailling_1(targets_copy);
            for(int blocker=0; blocker<64 && rook_to==-1; blocker++)
            {
                if(blocker==rook_from || blocker==candidate_to) continue;
                uint64_t occ = (1ULL<<rook_from) | (1ULL<<blocker);
                if(!(get_rook_attacks(rook_from, occ) & (1ULL<<candidate_to)))
                {
                    rook_to = candidate_to;
                    blocker_square = blocker;
                }
            }
        }
        expect(rook_to!=-1, "test setup: should find a rook move on an empty board that a real blocker can interrupt");

        BB board = BB();
        board.Board[1] = 1ULL << rook_from;   // white rook
        board.Board[0] = 1ULL << blocker_square; // white pawn as the blocker
        int wk = 0; while((board.Board[1]|board.Board[0]|(1ULL<<rook_to)) & (1ULL<<wk)) wk++;
        int bk = 63; while((board.Board[1]|board.Board[0]|(1ULL<<rook_to)|(1ULL<<wk)) & (1ULL<<bk)) bk--;
        board.Board[5] = 1ULL << wk;
        board.Board[11] = 1ULL << bk;

        bool legal = table.verify_move_is_legal_now(board, 1, Move(rook_from, rook_to));
        expect(!legal, "verify_move_is_legal_now: must reject a slide blocked by a real piece, even though the empty-board table says it's reachable");
    }

    // Moving into check: find a king step + an enemy rook placement such that
    // the destination square is attacked only AFTER the king arrives there
    // (king isn't currently in check - isolates the "moving into check" case).
    {
        int wk=-1, king_to=-1, enemy_rook_sq=-1;
        for(int candidate_wk=0; candidate_wk<64 && wk==-1; candidate_wk++)
        {
            uint64_t dests = K_template[candidate_wk];
            uint64_t dests_copy = dests;
            while(dests_copy && wk==-1)
            {
                int to = find_and_delete_trailling_1(dests_copy);
                for(int f=0; f<64; f++)
                {
                    if(f==candidate_wk || f==to) continue;
                    uint64_t occ_after  = (1ULL<<to) | (1ULL<<f);
                    uint64_t occ_before = (1ULL<<candidate_wk) | (1ULL<<f);
                    bool attacks_to_after = (get_rook_attacks(f, occ_after) & (1ULL<<to)) != 0;
                    bool attacks_wk_before = (get_rook_attacks(f, occ_before) & (1ULL<<candidate_wk)) != 0;
                    if(attacks_to_after && !attacks_wk_before)
                    {
                        wk=candidate_wk; king_to=to; enemy_rook_sq=f;
                        break;
                    }
                }
            }
        }
        expect(wk!=-1, "test setup: should find a king step that walks into an enemy rook's line");

        BB board = BB();
        board.Board[5] = 1ULL << wk;             // white king
        board.Board[7] = 1ULL << enemy_rook_sq;  // black rook
        int bk = 63; while((board.Board[5]|board.Board[7]|(1ULL<<king_to)) & (1ULL<<bk)) bk--;
        board.Board[11] = 1ULL << bk;             // black king, out of the way

        bool legal = table.verify_move_is_legal_now(board, 5, Move(wk, king_to));
        expect(!legal, "verify_move_is_legal_now: must reject a king move that walks into check");
    }

    std::cout << "All CuckooCycleTable probe checks passed." << std::endl;
    return 0;
}
