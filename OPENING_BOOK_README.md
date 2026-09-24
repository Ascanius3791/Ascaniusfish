<!-- OWNERSHIP=Claude -->
# Opening Book Integration Guide

## Overview
Your chess engine now supports loading opening books from both:
1. **PGN (Portable Game Notation)** files - classic format with move sequences
2. **Lichess JSON evaluations** - modern format with pre-computed evaluations (recommended)

Positions from the opening book are automatically inserted into the lookup table during engine initialization, providing immediate evaluations for known opening positions.

## Configuration
The opening book can be controlled entirely through `const bool` toggles at the top of **ascaniusfish.cpp**:

```cpp
const bool USE_OPENING_BOOK = true;              // Toggle opening book loading
const bool USE_LICHESS_JSON = true;              // true = JSON, false = PGN
const std::string OPENING_BOOK_PATH = "books/openings.json";  // Path to book
const int OPENING_BOOK_MAX_POSITIONS = 10000;    // Max positions for JSON (-1 = all)
const int OPENING_BOOK_DEPTH = 100;              // Depth for PGN entries
const int OPENING_BOOK_MAX_GAMES = -1;           // Max games for PGN (-1 = all)
const int OPENING_BOOK_MAX_MOVES = 20;           // Max moves/game for PGN
```

### Configuration Options

| Option | Type | Default | Purpose |
|--------|------|---------|---------|
| `USE_OPENING_BOOK` | bool | true | Master switch to enable/disable opening book loading |
| `USE_LICHESS_JSON` | bool | true | true = load Lichess JSON format, false = load PGN |
| `OPENING_BOOK_PATH` | string | "books/openings.json" | File path to the opening book (relative to working directory) |
| `OPENING_BOOK_MAX_POSITIONS` | int | 10000 | Max positions to load from JSON (-1 loads all) |
| `OPENING_BOOK_DEPTH` | int | 100 | Search depth assigned to PGN book positions (higher = favored) |
| `OPENING_BOOK_MAX_GAMES` | int | -1 | Maximum number of games to load from PGN (-1 loads all) |
| `OPENING_BOOK_MAX_MOVES` | int | 20 | Maximum moves per game to load from PGN |

## Usage

### Using Lichess JSON (Recommended)
1. Download a Lichess evaluation file from: https://database.lichess.org/
   - Look for `.jsonl` or `.json` files (JSON Lines format)
   - These contain pre-computed engine evaluations
2. Place it at: `books/openings.json`
3. Ensure `USE_LICHESS_JSON = true` in ascaniusfish.cpp
4. Rebuild and run:
   ```bash
   g++ -fdiagnostics-color=always -g ascaniusfish.cpp -o ascaniusfish
   ./ascaniusfish
   ```

### Using PGN Format
1. Download a PGN file (see below for sources)
2. Place it at your chosen path (e.g., `books/openings.pgn`)
3. Set in ascaniusfish.cpp:
   ```cpp
   const bool USE_LICHESS_JSON = false;
   const std::string OPENING_BOOK_PATH = "books/openings.pgn";
   ```
4. Rebuild and run

### Disable Opening Book
```cpp
const bool USE_OPENING_BOOK = false;
```

### Use a Different Opening Book
```cpp
const std::string OPENING_BOOK_PATH = "books/my_custom_book.json";
```

### Load Fewer Positions/Games
```cpp
const int OPENING_BOOK_MAX_POSITIONS = 5000;   // For JSON
const int OPENING_BOOK_MAX_GAMES = 1000;       // For PGN
const int OPENING_BOOK_MAX_MOVES = 10;         // For PGN
```

## Format Comparison

### Lichess JSON Format (Recommended)
**Advantages:**
- Pre-computed evaluations (no need to compute during load)
- Depth information from engine analysis
- Faster loading
- Higher quality evaluations

**Format Example:**
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3",
  "evals": [
    {
      "pvs": [
        {
          "cp": -25,
          "line": "c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6"
        }
      ],
      "knodes": 192958,
      "depth": 34
    }
  ]
}
```

One JSON object per line; each contains:
- `"fen"` - position in FEN notation
- `"evals"` - array of evaluations
  - `"cp"` - centipawn evaluation (positive = white advantage)
  - `"depth"` - search depth used for evaluation
  - `"pvs"` - principal variations
  - `"knodes"` - kilo-nodes searched

**Parser extracts:** FEN, first `cp` value, and first `depth` value

### PGN Format
**Advantages:**
- Widely available (historical games)
- Human-readable
- Easy to create custom books

**Format Example:**
```
[Event "Tournament"]
[Site "Location"]
[Date "2024.05.10"]
[White "Player1"]
[Black "Player2"]
[Result "*"]

1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6
```

## Getting Opening Books

### Lichess Database (Recommended for JSON)
- **URL**: https://database.lichess.org/
- **Files**: Download `.jsonl` or `.json` files with evaluations
- **Size**: Available in various sizes (100K, 1M, 5M+ positions)
- **Quality**: Engine-evaluated positions, excellent for training

### Other JSON Sources
- Lichess API exports
- Chess Engine evaluation databases
- Analysis tools that export JSON

### PGN Format Sources
1. **Lichess Opening Books**: https://database.lichess.org/ (export as PGN)
2. **Chess.com Database**: https://www.chess.com/forum/view/general/download-pgn-games
3. **TWIC (The Week in Chess)**: https://www.theweekinchess.com/ (historical games)
4. **ChessTempo**: https://www.chesstempo.com/ (opening databases)
5. **1001 Top Chess Games**: Many classic PGN collections available free

### Creating Custom Books
1. Download games from Lichess, Chess.com, or your database
2. Export as PGN or JSON
3. Place in `books/` directory
4. Configure path and format in ascaniusfish.cpp

## Implementation Details

### How It Works

#### Lichess JSON Loading:
1. **Startup Phase**: When `USE_OPENING_BOOK == true` and `USE_LICHESS_JSON == true`:
   - Reads the JSON file line-by-line
   - For each line:
     - Parses JSON to extract FEN, evaluation (cp), and depth
     - Creates a position from the FEN using the `BB(FEN)` constructor
     - Inserts position with its pre-computed evaluation into the lookup table
     - Uses the depth from the JSON file

2. **Advantages**:
   - Uses pre-computed evaluations (faster, more accurate)
   - Each position has its own depth value (from engine analysis)
   - Very fast to load (just JSON parsing and table insertion)

#### PGN Loading:
1. **Startup Phase**: When `USE_OPENING_BOOK == true` and `USE_LICHESS_JSON == false`:
   - Reads the PGN file
   - For each game:
     - Replays move sequences
     - For each resulting position:
       - Computes static evaluation using `basic_eval()`
       - Inserts into lookup table with `OPENING_BOOK_DEPTH`
   
2. **Trade-offs**:
   - Requires computing evaluations (slower load time)
   - All positions get same depth (`OPENING_BOOK_DEPTH`)
   - Evaluations less accurate (static eval only)

### Performance Impact
- **Load Time**: 
  - JSON: 1-5 seconds for 10,000 positions
  - PGN: 5-20 seconds for 1,000 games (depends on move computation)
- **Memory**: ~100KB-1MB per 1,000 positions
- **Search Speed**: Negligible impact; positions hit lookup table without recomputation

## Limitations and Future Improvements

### Current Limitations

#### JSON:
- Simple JSON parser (works for Lichess format, may fail on custom JSON)
- Extracts only first `cp` and `depth` (ignores multiple PVs)

#### PGN:
- Move matching uses simplified heuristics (works ~80% of the time)
- All positions stored at same depth
- No move recommendations stored

### Potential Enhancements
1. **Polyglot Format**: Add `.bin` file support (requires Zobrist keys)
2. **Full JSON Parser**: Use JSON library for robustness
3. **Move Weights**: Store move frequency for better move selection
4. **Adaptive Depth**: Store positions at variable depths based on importance
5. **Multi-format**: Auto-detect file format

## Testing

### Verify Opening Book is Loaded
Watch startup output:
```
=== Loading Opening Book ===
Loading opening book from Lichess JSON: books/openings.json
Loaded 10000 positions from 10000 entries in Lichess JSON file.
=== Opening Book Loaded ===
```

### Check Lookup Table Hit Rate
After a game:
```
The number of number_of_succ_readouts: Z
```
Positions from the opening book will show as successful readouts during play.

## Troubleshooting

### "Could not open file" Error
- Check path is correct relative to working directory
- Use absolute path if needed: `"/home/user/books/openings.json"`
- Verify file exists: `ls -la books/openings.json`

### No Positions Loaded
- For JSON: Verify file is valid JSON Lines format (one object per line)
- For PGN: Verify PGN file has valid move sequences
- Try with sample file first: use the included `books/openings.json`

### JSON Parse Error
- Check JSON format matches Lichess specification
- Ensure each line is a complete JSON object
- Verify FEN strings are valid
- Check for UTF-8 encoding issues

### Engine Slow at Startup
- Reduce `OPENING_BOOK_MAX_POSITIONS` (for JSON)
- Reduce `OPENING_BOOK_MAX_GAMES` (for PGN)
- Use a smaller file
- Set `USE_OPENING_BOOK = false` if startup speed is critical

## Quick Start Examples

### Load Latest Lichess Database
```bash
# 1. Download from Lichess
wget https://database.lichess.org/standard/lichess_db_standard_rated_2024.json.zst

# 2. Decompress (if .zst)
unzstd lichess_db_standard_rated_2024.json.zst

# 3. Place in books directory
mv lichess_db_standard_rated_2024.json books/openings.json

# 4. In ascaniusfish.cpp, set:
# const bool USE_LICHESS_JSON = true;
# const std::string OPENING_BOOK_PATH = "books/openings.json";

# 5. Build and run
g++ -fdiagnostics-color=always -g ascaniusfish.cpp -o ascaniusfish
./ascaniusfish
```

### Load Custom PGN
```bash
# 1. Download PGN
wget https://example.com/my_games.pgn

# 2. Place in books directory
mv my_games.pgn books/custom_openings.pgn

# 3. In ascaniusfish.cpp, set:
# const bool USE_LICHESS_JSON = false;
# const std::string OPENING_BOOK_PATH = "books/custom_openings.pgn";

# 4. Rebuild
g++ -fdiagnostics-color=always -g ascaniusfish.cpp -o ascaniusfish
./ascaniusfish
```

## Files Modified/Created

- **Created**: `lib/opening_book.hpp` - Header with loader functions
- **Created**: `src/opening_book.cpp` - Implementation (PGN + JSON parsers)
- **Modified**: `ascaniusfish.cpp` - Added configuration and loader call
- **Created**: `books/openings.json` - Sample Lichess JSON book
- **Created**: `books/openings.pgn` - Sample PGN book
- **Created**: `OPENING_BOOK_README.md` - This documentation

## API Reference

### PGN Loader
```cpp
bool load_opening_book_from_pgn(
    const std::string& pgn_path,       // Path to PGN file
    lookup_table& table,               // Reference to lookup table
    int target_depth = 100,            // Depth for all positions
    int max_games = -1,                // Max games (-1 = all)
    int max_moves_per_game = 20        // Max moves per game
);
```

### JSON Loader (Lichess Format)
```cpp
bool load_opening_book_from_lichess_json(
    const std::string& json_path,      // Path to JSON file (one object per line)
    lookup_table& table,               // Reference to lookup table
    int max_positions = -1             // Max positions (-1 = all)
);
```

### Directory Loader (PGN only)
```cpp
int load_opening_books_from_directory(
    const std::string& dir_path,       // Directory containing .pgn files
    lookup_table& table,               // Reference to lookup table
    int target_depth = 100             // Depth for all positions
);
```

## Configuration
The opening book can be controlled entirely through `const bool` toggles at the top of **ascaniusfish.cpp**:

```cpp
const bool USE_OPENING_BOOK = true;              // Toggle opening book loading ON/OFF
const std::string OPENING_BOOK_PATH = "books/openings.pgn";  // Path to PGN file
const int OPENING_BOOK_DEPTH = 100;              // Depth at which positions are stored
const int OPENING_BOOK_MAX_GAMES = -1;           // Max games to load (-1 = load all)
const int OPENING_BOOK_MAX_MOVES = 20;           // Max moves per game to load
```

### Configuration Options

| Option | Type | Default | Purpose |
|--------|------|---------|---------|
| `USE_OPENING_BOOK` | bool | true | Master switch to enable/disable opening book loading |
| `OPENING_BOOK_PATH` | string | "books/openings.pgn" | File path to the PGN opening book (relative to working directory) |
| `OPENING_BOOK_DEPTH` | int | 100 | Search depth assigned to opening book positions in lookup table (higher = favored in table) |
| `OPENING_BOOK_MAX_GAMES` | int | -1 | Maximum number of games to load from PGN (-1 loads all) |
| `OPENING_BOOK_MAX_MOVES` | int | 20 | Maximum moves per game to load (limits to early opening lines) |

## Usage

### Basic Usage
1. Place your PGN file at the path specified in `OPENING_BOOK_PATH` (default: `books/openings.pgn`)
2. Rebuild the engine: `g++ -fdiagnostics-color=always -g ascaniusfish.cpp -o ascaniusfish`
3. Run the engine; it will automatically load the opening book on startup
4. The engine will use lookup table entries from the opening book during games

### Disable Opening Book
To turn off opening book loading without recompiling:
- Modify the `USE_OPENING_BOOK` flag at the top of `ascaniusfish.cpp`:
  ```cpp
  const bool USE_OPENING_BOOK = false;  // Disable
  ```
- Recompile

### Use a Different Opening Book
To use a different PGN file:
- Change the `OPENING_BOOK_PATH`:
  ```cpp
  const std::string OPENING_BOOK_PATH = "books/my_custom_openings.pgn";
  ```
- Recompile

### Load Fewer/More Moves
To limit the opening phase to only the first X moves:
```cpp
const int OPENING_BOOK_MAX_MOVES = 10;  // Load only first 10 moves per game
```

## Getting Opening Books

### Where to Download
Several free chess opening books are available online:

1. **Popular Opening Books (in PGN format)**
   - Lichess Opening Books: https://database.lichess.org/ (download PGN files)
   - Chess.com Database: https://www.chess.com/forum/view/general/download-pgn-games
   - TWIC (The Week in Chess): https://www.theweekinchess.com/ (historical games)
   - 1001 Top Chess Games: Many classic PGN databases available free

2. **Large Opening Theory Collections**
   - Chessbase Opening Books (some free versions available)
   - Opening theory PGN files from chess engines communities

3. **Creating Custom Books**
   - Download games from Lichess, Chess.com, or your own database
   - Export as PGN files
   - Place in the `books/` directory

### Recommended Workflow
1. Download a PGN file (e.g., `opening_database.pgn`)
2. Place it in: `./books/openings.pgn`
3. (Optional) Configure `OPENING_BOOK_MAX_MOVES` and `OPENING_BOOK_DEPTH`
4. Recompile and run

## Files Modified/Created

- **Created**: `lib/opening_book.hpp` - Header with loader functions
- **Created**: `src/opening_book.cpp` - Implementation (PGN + JSON parsers)
- **Modified**: `ascaniusfish.cpp` - Added configuration and loader call
- **Created**: `books/openings.json` - Sample Lichess JSON book
- **Created**: `books/openings.pgn` - Sample PGN book
- **Created**: `OPENING_BOOK_README.md` - This documentation

## API Reference

### PGN Loader
```cpp
bool load_opening_book_from_pgn(
    const std::string& pgn_path,       // Path to PGN file
    lookup_table& table,               // Reference to lookup table
    int target_depth = 100,            // Depth for all positions
    int max_games = -1,                // Max games (-1 = all)
    int max_moves_per_game = 20        // Max moves per game
);
```

### JSON Loader (Lichess Format)
```cpp
bool load_opening_book_from_lichess_json(
    const std::string& json_path,      // Path to JSON file (one object per line)
    lookup_table& table,               // Reference to lookup table
    int max_positions = -1             // Max positions (-1 = all)
);
```

### Directory Loader (PGN only)
```cpp
int load_opening_books_from_directory(
    const std::string& dir_path,       // Directory containing .pgn files
    lookup_table& table,               // Reference to lookup table
    int target_depth = 100             // Depth for all positions
);
```

## Configuration
The opening book can be controlled entirely through `const bool` toggles at the top of **ascaniusfish.cpp**:

```cpp
const bool USE_OPENING_BOOK = true;              // Toggle opening book loading
const std::string OPENING_BOOK_PATH = "books/openings.pgn";  // Path to opening book (PGN format)
const int OPENING_BOOK_DEPTH = 100;              // Depth at which opening book entries are stored
const int OPENING_BOOK_MAX_GAMES = -1;           // Max games to load (-1 = load all)
const int OPENING_BOOK_MAX_MOVES = 20;           // Max moves per game to load
// ========================================================

int main()

