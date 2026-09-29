// OWNERSHIP=Claude
// Interactive terminal viewer for WEIGHTS::piece_table_value_opening/endgame
// (lib/Weights.hpp): prints one piece's per-square value as a colour-coded
// number.
//
// It shows the raw piece_table_value_opening/endgame[row][square] entry,
// exactly as stored in WEIGHTS: the literal "piece square table". (A CENTRAL
// mode for central_pawn_presence() went with that function, #36.)
//
// king_safety_of_colour and piece_activity_eval are deliberately NOT included
// here: they're meaningful only in a fully set-up position (they score
// distance-to-king and mobility against real occupancy), not for one piece
// on an empty board, and - the concrete reason - king_safety_of_colour
// unconditionally does __builtin_ctzll(Board[king]), which is undefined
// behaviour once a king bitboard can be empty. Verified with
// -fsanitize=address,undefined on a kingless board: ctz(0) fires (UBSan),
// the resulting garbage "king square" then feeds get_attacked_squares()'s
// K_template[king_sq] lookup in magics.cpp, and that read walked off the end
// of the 64-entry global template array - a real global-buffer-overflow,
// caught by ASan. In a non-sanitized build this doesn't crash, it just reads
// adjacent globals and silently corrupts the mobility term. material_eval()
// has a similar edge (its sqrt() argument assumes material_left is always
// >= ~140.5, true whenever both kings are on the board - 350 each - but not
// otherwise), so it's avoided here too, on top of it not being a
// square-dependent term in the first place. piecetable() was checked the same
// way and has no such assumption - it iterates bitboards purely via
// while(bb){...find_and_delete_trailling_1}, a no-op on an empty bitboard
// rather than UB.
//
// WEIGHTS only stores 7 distinct rows (piece_table_value_opening[7][64]):
// white pawn, rook, knight, bishop, queen, king/black-king (shared, row 5 is
// used unflipped for both colours), and black pawn. Non-pawn black pieces are
// NOT a mirrored copy of the white table - piecetable() in basic_eval.cpp
// indexes black pieces with the same unflipped square index as white, so
// e.g. a black knight on e5 is scored with the literal [2][e5] entry, not a
// rank-flipped one. This viewer follows that: it cycles the 7 stored rows,
// not 12 piece/colour combinations.
//
// Build (from repo root, per CLAUDE.md's diagnostics/ convention):
//   g++ -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable \
//       -DNDEBUG -o diagnostics/piece_square_table_viewer \
//       diagnostics/piece_square_table_viewer.cpp
//
// Controls: n/p or Right/Left = next/prev piece row, t = toggle opening/
// endgame, q/Esc = quit.

#include "../ascaniusfish.hpp"
#include <cstdio>
#include <string>
#include <termios.h>
#include <unistd.h>

struct Row_Info {
    int index;      // row into WEIGHTS::piece_table_value_opening/endgame
    char fen_char;   // piece placed on the probe square
    bool is_white;    // which side the probe piece belongs to
    const char* label;
};

static const Row_Info ROWS[7] = {
    {0, 'P', true,  "White Pawn   (row 0)"},
    {1, 'R', true,  "Rook         (row 1, shared W/B table)"},
    {2, 'N', true,  "Knight       (row 2, shared W/B table)"},
    {3, 'B', true,  "Bishop       (row 3, shared W/B table)"},
    {4, 'Q', true,  "Queen        (row 4, shared W/B table)"},
    {5, 'K', true,  "King         (row 5, shared W/B table)"},
    {6, 'p', false, "Black Pawn   (row 6)"},
};

bool pawn_edge_square(const Row_Info& row, int sq) {
    return (row.index == 0 || row.index == 6) && (sq < 8 || sq >= 56); // pawns can't stand on rank 1/8
}

// piecetable() blends opening/endgame by material left on the board, which
// on a one-piece board is always near-empty regardless of which table you
// want to look at. This reimplements just the per-piece lookup with a fixed
// weight instead, so the opening/endgame toggle actually shows that table.
int piecetable_fixed_phase(const Row_Info& row, int sq, const WEIGHTS& W, bool opening) {
    return opening ? W.piece_table_value_opening[row.index][sq]
                    : W.piece_table_value_endgame[row.index][sq];
}

void set_bg(int r, int g, int b) { std::printf("\x1b[48;2;%d;%d;%dm\x1b[30m", r, g, b); }
void reset_col() { std::printf("\x1b[0m"); }

void colour_for(int v, int max_abs, int& r, int& g, int& b) {
    if (max_abs <= 0) max_abs = 1;
    double t = std::min(1.0, std::abs((double)v) / max_abs);
    int shade = (int)(215 * t);
    if (v >= 0) { r = 255 - shade; g = 255; b = 255 - shade; }
    else        { r = 255; g = 255 - shade; b = 255 - shade; }
}

void render(const Row_Info& row, const WEIGHTS& W, bool opening) {
    std::printf("\x1b[2J\x1b[H"); // clear screen, home cursor
    std::printf("Piece-square table viewer  --  %s\n", row.label);
    std::printf("Table: %s\n", opening ? "opening" : "endgame");
    std::printf("[n/p or arrows] piece   [t] opening/endgame   [q] quit\n\n");

    int values[64];
    bool valid[64];
    int max_abs = 0;
    for (int sq = 0; sq < 64; sq++) {
        valid[sq] = !pawn_edge_square(row, sq);
        values[sq] = piecetable_fixed_phase(row, sq, W, opening);
        if (valid[sq] && std::abs(values[sq]) > max_abs) max_abs = std::abs(values[sq]);
    }

    for (int rank = 7; rank >= 0; rank--) {
        std::printf("%d ", rank + 1);
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            if (!valid[sq]) {
                std::printf(" %5s", "·");
                continue;
            }
            int r, g, b;
            colour_for(values[sq], max_abs, r, g, b);
            set_bg(r, g, b);
            std::printf(" %5d", values[sq]);
            reset_col();
        }
        std::printf("\n");
    }
    std::printf("  ");
    for (int file = 0; file < 8; file++) std::printf("     %c", 'a' + file);
    std::printf("\n\nmax |value| shown: %d   (no king on the board; '.' = not applicable)\n", max_abs);
}

struct Raw_Mode_Guard {
    termios old_t{};
    Raw_Mode_Guard() {
        termios t;
        tcgetattr(STDIN_FILENO, &t);
        old_t = t;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    ~Raw_Mode_Guard() { tcsetattr(STDIN_FILENO, TCSANOW, &old_t); }
};

int main() {
    init_magics();
    init_sliders_attacks(1);
    init_sliders_attacks(0);

    WEIGHTS W = WEIGHTS_OG;
    int row_i = 0;
    bool opening = true;

    Raw_Mode_Guard guard;
    render(ROWS[row_i], W, opening);
    while (true) {
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) != 1) break;
        if (c == 'q') break;
        if (c == 'n' || c == 'l') row_i = (row_i + 1) % 7;
        else if (c == 'p' || c == 'h') row_i = (row_i + 6) % 7;
        else if (c == 't') opening = !opening;
        else if (c == 27) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                if (seq[0] == '[' && seq[1] == 'C') row_i = (row_i + 1) % 7;      // Right
                else if (seq[0] == '[' && seq[1] == 'D') row_i = (row_i + 6) % 7; // Left
            } else {
                break; // bare Esc
            }
        } else {
            continue;
        }
        render(ROWS[row_i], W, opening);
    }
    std::printf("\n");
    return 0;
}
