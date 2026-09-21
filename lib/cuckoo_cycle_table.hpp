// OWNERSHIP=Claude
#ifndef CUCKOO_CYCLE_TABLE_HPP
#define CUCKOO_CYCLE_TABLE_HPP

#include "../lib/checks.hpp"       // Kn_template/K_template, get_bishop_attacks/get_rook_attacks, init_magics/init_sliders_attacks
#include "../lib/zobrist.hpp"      // Zobrist::pieceKeys
#include "../lib/move_generation.hpp" // Move

#include <cstdint>

// Building block for cheap "cycle" (upcoming/possible repetition) detection,
// based on the technique used by Stockfish (see the chess programming wiki's
// "cuckoo hashing" repetition-detection article). Wired into minimax() via
// detect_upcoming_cycle() below (see ascaniusfish_2.hpp). This file only
// depends on already-initialized Zobrist keys and slider-attack tables, so it
// can also be built and tested in isolation (call init_magics();
// init_sliders_attacks(1); init_sliders_attacks(0); and construct a Zobrist()
// before constructing a CuckooCycleTable - main() already does this at startup,
// before any real move is requested, so the engine's own use needs no extra care).
//
// THE CORE IDEA: a quiet (non-capture, non-pawn, non-castling) move of one piece
// from square A to square B changes the Zobrist hash by exactly
//   Zobrist::pieceKeys[piece][A] XOR Zobrist::pieceKeys[piece][B]
// - a value that depends only on (piece, A, B), never on the rest of the board,
// and moving the piece back XORs the identical value in again (XOR is its own
// inverse). So if two positions' hashes differ by exactly one such value, they
// are connected by exactly one reversible move - a sign the search is on or next
// to a repetition cycle. This class precomputes every possible such delta once
// and stores it in a hash table (using cuckoo hashing, so every lookup is at
// most two array reads) mapping delta -> the move that produces it.
//
// The table itself (probe()) works on a *pure piece-square* delta only. On top
// of that, probe_with_ply_gap()/probe_with_board_check() below handle the two
// gaps called out earlier:
//   - probe_with_ply_gap() folds in Zobrist::sideKey correctly, since every
//     real move toggles it exactly once (see adjust_for_side_to_move() in
//     src/zobrist.cpp) - an odd ply gap between the two positions leaves one
//     net toggle, an even gap cancels it out.
//   - probe_with_board_check() additionally verifies the found piece is
//     actually sitting on one of the move's two squares in a real BB, rejecting
//     the rare false positive where a delta coincidentally has the right shape
//     but isn't actually present, and canonicalizing the move's direction to
//     start from wherever the piece truly is.
//
// probe_full() below closes the last two gaps: a castling-rights change
// contributes a castlingKeys[old]^castlingKeys[new] term, and en-passant keys
// can contribute a nonzero term even on an otherwise-quiet move if it's played
// the ply right after a double pawn push (which always clears the ep flag).
// Unlike the side-to-move key, neither of these is predictable just from knowing
// how many plies apart two positions are - it depends on which moves were
// actually played - so there's no XOR trick to cancel them the way
// probe_with_ply_gap() cancels sideKey. Instead probe_full() takes both real
// boards and requires their castling rights and en-passant square to be
// identical before trusting a match: only then does the found move actually
// represent a genuine "same position, one reversible move away" relationship -
// full position identity (piece placement + side to move + castling rights +
// en passant) is exactly what the threefold-repetition rule itself compares.
// (In practice, differing rights/en-passant already pollute the raw hash
// difference with an essentially-random extra term that a stored delta almost
// never happens to match anyway - but that was a probabilistic argument riding
// on hash-collision-improbability, not a real guarantee, so probe_full() checks
// it explicitly instead.)
//
// Pawns and castling moves are excluded from the table entirely - they're never
// reversible, so they can never be part of a repeating cycle.

struct CuckooEntry
{
    uint64_t key = 0;   // 0 means "empty slot" - a real delta being exactly 0 would
                        // require two distinct Zobrist keys to collide exactly,
                        // astronomically unlikely for randomly generated keys and
                        // not guarded against, same as elsewhere in this codebase.
    int piece_type = -1; // index into Zobrist::pieceKeys (0-11), see BB::Board's convention
    Move move;
};

class CuckooCycleTable
{
    public:
    CuckooCycleTable(); // builds the table - see preconditions above

    // Looks up whether `delta` matches some single reversible move. Returns
    // true and fills piece_type_out/move_out if found. This is the raw,
    // pure-piece-square-only check - see probe_with_ply_gap()/
    // probe_with_board_check() for the versions that account for the side-to-
    // move key and real board state.
    bool probe(uint64_t delta, int& piece_type_out, Move& move_out) const;

    // hash_a/hash_b are two positions' full Zobrist hashes, ply_gap plies apart
    // (either order). Folds in the correct number of Zobrist::sideKey toggles
    // for that gap before probing - see the class comment for what's still not
    // accounted for (castling rights / en passant).
    bool probe_with_ply_gap(uint64_t hash_a, uint64_t hash_b, int ply_gap, int& piece_type_out, Move& move_out) const;

    // Same as probe_with_ply_gap(), but also checks the found piece is actually
    // on one of the move's two squares in board_at_one_end (the real board at
    // whichever of the two positions the caller has on hand) - rejecting the
    // rare false positive where the delta merely has the right shape, and
    // orienting move_out to start from wherever the piece truly is.
    bool probe_with_board_check(uint64_t hash_a, uint64_t hash_b, int ply_gap, const BB& board_at_one_end, int& piece_type_out, Move& move_out) const;

    // The fullest, most correct version: takes BOTH real boards, so it can also
    // require castling rights and en-passant availability to be identical at
    // the two endpoints (see the class comment) before trusting a match found
    // via probe_with_board_check(). This is what real cycle detection should
    // actually call; the hash-only overloads above remain useful when only
    // hashes (not full boards) are available, e.g. a compact history array.
    bool probe_full(const BB& board_a, const BB& board_b, int ply_gap, int& piece_type_out, Move& move_out) const;

    // probe_full() only proves the delta/board-identity math checks out - it
    // does NOT prove the move is actually playable in `board` right now: the
    // destination could be occupied (the table only ever represents quiet,
    // non-capture moves), a sliding piece's path could be blocked by a real
    // piece (the table was built assuming an empty board), or the move could
    // leave the mover's own king in check. This checks all three against the
    // real board, reusing the existing attack tables/in_check() rather than
    // reimplementing move generation.
    bool verify_move_is_legal_now(const BB& board, int piece_type, const Move& move) const;

    // The single call site minimax() needs: current_board is the node being
    // searched, ancestor_board is some earlier position from the search path,
    // ply_gap is how many plies apart they are (must be ODD for this to mean
    // anything - see the note on probe_full()/ply parity in
    // src/cuckoo_cycle_table.cpp). Combines probe_full() with
    // verify_move_is_legal_now(): true only if both agree this is a real,
    // currently-legal move that would recreate ancestor_board.
    //
    // CALLER TRAP: never call this with ply_gap==1 (ancestor_board = the
    // immediate parent). Undoing whatever move was just played to reach
    // current_board is ALWAYS available for any quiet move - that's simply
    // what "reversible" means, not a cycle - so it would return true for
    // nearly every quiet move a caller ever asks about. This was a real bug
    // in minimax()'s integration: it scored almost every non-capture move as
    // an instant draw. Only ply_gap 3, 5, 7, ... are meaningful.
    bool detect_upcoming_cycle(const BB& current_board, const BB& ancestor_board, int ply_gap, int& piece_type_out, Move& move_out) const;

    int size() const { return TABLE_SIZE; }
    int entries_stored() const { return stored_count; }

    private:
    static constexpr int TABLE_BITS = 15;
    static constexpr int TABLE_SIZE = 1 << TABLE_BITS; // 32768 - sized for a load
                                                        // factor well under 50%
                                                        // (roughly ~7000 real
                                                        // entries get inserted)
    static constexpr int MAX_INSERT_ITERATIONS = 100;

    CuckooEntry table[TABLE_SIZE];
    int stored_count = 0;

    static int h1(uint64_t key);
    static int h2(uint64_t key);
    void insert(uint64_t key, int piece_type, Move move);
    void populate();
};

// Not part of ascaniusfish.hpp's central include chain (ascaniusfish_2.hpp
// includes this header directly instead), so it pulls in its own
// implementation, the same way lib/RuntimeSettings.hpp does.
#include "../src/cuckoo_cycle_table.cpp"

#endif // CUCKOO_CYCLE_TABLE_HPP
