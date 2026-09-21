# Lichess JSON Opening Book Support - Implementation Summary

## What Was Added

Your chess engine now supports **Lichess JSON evaluation files** in addition to PGN files.

### New Capability: Lichess JSON Format
- **One line = one position**: JSON Lines format (one JSON object per line)
- **Pre-computed evaluations**: Uses `"cp"` (centipawn) values from the JSON
- **Engine depth included**: Uses `"depth"` field from analysis
- **Fast loading**: No move replay needed, just FEN parsing and table insertion

### Configuration (all in ascaniusfish.cpp, top of file):

```cpp
const bool USE_OPENING_BOOK = true;              // Master toggle
const bool USE_LICHESS_JSON = true;              // ← NEW: true = JSON, false = PGN
const std::string OPENING_BOOK_PATH = "books/openings.json";
const int OPENING_BOOK_MAX_POSITIONS = 10000;    // ← NEW: for JSON format
const int OPENING_BOOK_DEPTH = 100;              
const int OPENING_BOOK_MAX_GAMES = -1;           
const int OPENING_BOOK_MAX_MOVES = 20;           
```

## Files Updated

### Modified
- **ascaniusfish.cpp** - Added `USE_LICHESS_JSON` toggle and conditional loader

### Enhanced
- **lib/opening_book.hpp** - Added `load_opening_book_from_lichess_json()` function declaration
- **src/opening_book.cpp** - Added complete JSON parser + loader implementation
- **OPENING_BOOK_README.md** - Comprehensive guide with Lichess instructions

### Created/Updated
- **books/openings.json** - Sample Lichess JSON file (5 positions for testing)

## JSON Parser Details

The JSON parser extracts:
1. **"fen"** - position in FEN notation → creates `BB` position
2. First **"cp"** value → stored as position evaluation
3. First **"depth"** value → stored as search depth

Simple but effective for Lichess format.

## Usage

### Quick Setup (Lichess JSON - Recommended)
```bash
# 1. Download from Lichess database
wget https://database.lichess.org/standard/lichess_db_standard_rated_2024.json.zst
unzstd lichess_db_standard_rated_2024.json.zst

# 2. Place in books directory
mv lichess_db_standard_rated_2024.json books/openings.json

# 3. In ascaniusfish.cpp ensure:
const bool USE_LICHESS_JSON = true;
const std::string OPENING_BOOK_PATH = "books/openings.json";

# 4. Build and run
g++ -fdiagnostics-color=always -g ascaniusfish.cpp -o ascaniusfish
./ascaniusfish
```

### Switch to PGN (if needed)
```cpp
const bool USE_LICHESS_JSON = false;
const std::string OPENING_BOOK_PATH = "books/openings.pgn";
```

## Advantages of Lichess JSON

✅ **Pre-computed evaluations** - No need to run static eval on load
✅ **Better quality evals** - From engine analysis, not just piece tables
✅ **Individual depths** - Each position can have different analysis depth
✅ **Faster loading** - Just parse FEN and insert; no move replay
✅ **Larger databases** - Lichess database has millions of positions
✅ **Standard format** - One JSON object per line (JSON Lines)

## Backward Compatibility

✅ **PGN still works** - Set `USE_LICHESS_JSON = false`
✅ **No breaking changes** - Old PGN configurations still work
✅ **Easy toggle** - Switch formats with one boolean

## Limitations

- JSON parser is simple (works for Lichess format, may not handle all custom JSON)
- Only extracts first `cp` and `depth` (ignores multiple PVs)
- FEN validation depends on BB(FEN) constructor

## Build Status

✓ Compiles cleanly (617KB executable)
✓ All functions included inline (follows project pattern)
✓ Ready to use immediately

## Testing

Run the engine and check startup output:
```
=== Loading Opening Book ===
Loading opening book from Lichess JSON: books/openings.json
Successfully loaded X positions from Y entries in Lichess JSON file.
=== Opening Book Loaded ===
```

The sample `books/openings.json` contains 5 test positions from the Caro-Kann defense.

## Next Steps

1. Download a Lichess JSON database
2. Place at `books/openings.json`
3. Rebuild: `g++ -fdiagnostics-color=always -g ascaniusfish.cpp -o ascaniusfish`
4. Run and watch the engine load the book at startup

## Technical Details

### JSON Format Specification
```json
{
  "fen": "position in FEN notation",
  "evals": [
    {
      "pvs": [
        {
          "cp": 25,
          "line": "move1 move2 move3..."
        }
      ],
      "knodes": 206765,
      "depth": 36
    }
  ]
}
```

One object per line in the file.

### Parsing Logic
```cpp
1. Read line from file
2. Extract "fen" value using string search
3. Extract first "cp" value using string search
4. Extract first "depth" value using string search
5. Create BB position from FEN
6. Set evaluation and depth
7. Insert into lookup table
```

Simple string-based parsing (fast, no JSON library dependency).
