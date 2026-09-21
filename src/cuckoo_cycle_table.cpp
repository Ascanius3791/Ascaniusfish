// OWNERSHIP=Claude
#ifndef CUCKOO_CYCLE_TABLE_CPP
#define CUCKOO_CYCLE_TABLE_CPP

#include "../lib/cuckoo_cycle_table.hpp"
#include "../lib/bit_operations.hpp"

#include <iostream>

int CuckooCycleTable::h1(uint64_t key)
{
    return static_cast<int>(key & (TABLE_SIZE - 1));
}

int CuckooCycleTable::h2(uint64_t key)
{
    // A different bit window of the same key, so h1/h2 behave close to
    // independently even though they're derived from one hash value - the
    // Zobrist keys themselves are already high-quality random 64-bit values.
    return static_cast<int>((key >> 32) & (TABLE_SIZE - 1));
}

void CuckooCycleTable::insert(uint64_t key, int piece_type, Move move)
{
    int slot = h1(key);
    for(int iteration=0; iteration<MAX_INSERT_ITERATIONS; iteration++)
    {
        std::swap(key, table[slot].key);
        std::swap(piece_type, table[slot].piece_type);
        std::swap(move, table[slot].move);

        if(key==0)//the slot we just displaced was empty - done
        {
            stored_count++;
            return;
        }
        //we're now holding whatever entry used to live in `slot` - it must live
        //in its OTHER candidate slot, so bounce it there next
        slot = (slot==h1(key)) ? h2(key) : h1(key);
    }
    std::cout << "Error: CuckooCycleTable insertion failed after " << MAX_INSERT_ITERATIONS
               << " iterations - table is too full or hash functions are inadequate!" << std::endl;
    exit(1);
}

bool CuckooCycleTable::probe(uint64_t delta, int& piece_type_out, Move& move_out) const
{
    for(int slot : {h1(delta), h2(delta)})
    {
        if(table[slot].key==delta)
        {
            piece_type_out = table[slot].piece_type;
            move_out = table[slot].move;
            return true;
        }
    }
    return false;
}

bool CuckooCycleTable::probe_with_ply_gap(uint64_t hash_a, uint64_t hash_b, int ply_gap, int& piece_type_out, Move& move_out) const
{
    uint64_t delta = hash_a ^ hash_b;
    if(ply_gap % 2 != 0)
    {
        //an odd number of real moves happened between the two positions, and
        //every move toggles sideKey exactly once (adjust_for_side_to_move() in
        //src/zobrist.cpp) - an odd count leaves one net toggle in the raw
        //difference that a single reversible move's stored (pure piece-square)
        //delta doesn't include, so cancel it back out before probing.
        delta ^= Zobrist::sideKey;
    }
    return probe(delta, piece_type_out, move_out);
}

bool CuckooCycleTable::probe_with_board_check(uint64_t hash_a, uint64_t hash_b, int ply_gap, const BB& board_at_one_end, int& piece_type_out, Move& move_out) const
{
    int piece_type;
    Move move;
    if(!probe_with_ply_gap(hash_a, hash_b, ply_gap, piece_type, move))
    return false;

    uint64_t piece_bitboard = board_at_one_end.Board[piece_type];
    bool piece_on_from = (piece_bitboard & (1ULL << move.from)) != 0;
    bool piece_on_to   = (piece_bitboard & (1ULL << move.to))   != 0;

    if(!piece_on_from && !piece_on_to)
    return false; //the delta had the right shape, but this piece isn't actually
                  //at either endpoint in the position we can check - discard

    //orient the returned move to start from wherever the piece actually is
    //(recall the delta is symmetric, so probe() may have returned either direction)
    move_out = piece_on_from ? move : Move(move.to, move.from);
    piece_type_out = piece_type;
    return true;
}

bool CuckooCycleTable::probe_full(const BB& board_a, const BB& board_b, int ply_gap, int& piece_type_out, Move& move_out) const
{
    //castling rights and en-passant availability must both be identical at the
    //two endpoints - otherwise whatever connects them permanently changed legal
    //state (e.g. a king/rook's first move, or a double pawn push clearing the ep
    //flag), so it can't be part of a genuine repeat, whatever the piece-square
    //delta says. This can't be derived from ply_gap the way the sideKey toggle
    //can - it depends on which moves were actually played - so it's checked
    //directly against real board state instead.
    bool same_castling = board_a.castle[0][0]==board_b.castle[0][0]
                       && board_a.castle[0][1]==board_b.castle[0][1]
                       && board_a.castle[1][0]==board_b.castle[1][0]
                       && board_a.castle[1][1]==board_b.castle[1][1];
    bool same_en_passant = board_a.en_passant==board_b.en_passant;
    if(!same_castling || !same_en_passant)
    return false;

    return probe_with_board_check(board_a.zobrist_hash, board_b.zobrist_hash, ply_gap, board_a, piece_type_out, move_out);
}

void CuckooCycleTable::populate()
{
    //piece_type indices follow BB::Board's convention: 0-5 white (pawn, rook,
    //knight, bishop, queen, king), 6-11 the same for black. Pawns are skipped -
    //pawn moves are never reversible, so they can never sit on a repetition cycle.
    for(int color_offset : {0, 6})
    {
        for(int from=0; from<64; from++)
        {
            uint64_t reachable_knight = Kn_template[from];
            uint64_t reachable_king   = K_template[from];
            uint64_t reachable_rook   = get_rook_attacks(from, 0ULL);
            uint64_t reachable_bishop = get_bishop_attacks(from, 0ULL);
            uint64_t reachable_queen  = reachable_rook | reachable_bishop;

            struct { int piece_type; uint64_t reachable; } pieces[] =
            {
                {1+color_offset, reachable_rook},
                {2+color_offset, reachable_knight},
                {3+color_offset, reachable_bishop},
                {4+color_offset, reachable_queen},
                {5+color_offset, reachable_king},
            };

            for(auto& p : pieces)
            {
                uint64_t destinations = p.reachable;
                while(destinations)
                {
                    int to = find_and_delete_trailling_1(destinations);
                    //reachability is symmetric for every piece type here (on an
                    //empty board, A can reach B iff B can reach A), so A->B and
                    //B->A produce the IDENTICAL delta - only insert once per
                    //undirected pair, or the duplicate key would make cuckoo
                    //insertion bounce forever between the same two slots.
                    if(to>from)
                    {
                        uint64_t delta = Zobrist::pieceKeys[p.piece_type][from] ^ Zobrist::pieceKeys[p.piece_type][to];
                        insert(delta, p.piece_type, Move(from, to));
                    }
                }
            }
        }
    }
}

CuckooCycleTable::CuckooCycleTable()
{
    populate();
}

#endif // CUCKOO_CYCLE_TABLE_CPP
