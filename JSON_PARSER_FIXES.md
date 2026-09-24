<!-- OWNERSHIP=Claude -->
# JSON Parser Fixes - Summary

## Issues Fixed

### 1. **Parser Failing on Partial Lines** ✓
**Problem**: Parser was trying to parse every line including incomplete ones (formatting lines, closing braces, etc.)
**Solution**: Added quick check - skip any line that doesn't contain `"fen"` keyword

### 2. **Only Using First Evaluation** ✓
**Problem**: Parser was extracting only the first `"cp"` value, ignoring higher-quality evaluations
**Solution**: Modified parser to find ALL `"cp"` values and keep the one with the **HIGHEST depth** (best analysis)

### 3. **Negative Values Not Handled** ✓
**Problem**: Parser didn't handle negative centipawn values (black advantage)
**Solution**: Added handling for negative sign before parsing integers

### 4. **Using Engine Depth Instead of INT_MAX** ✓
**Problem**: Parser was storing evaluations with the engine's analysis depth
**Solution**: Store all positions with `depth_of_eval = INT_MAX` to prioritize book entries over search results

### 5. **Too Many Warning Messages** ✓
**Problem**: Parser printed warnings for every unparseable line (formatting, partial lines, etc.)
**Solution**: Silently skip invalid lines; only report final statistics

## Code Changes

### Modified: src/opening_book.cpp

#### 1. Added `#include <climits>` for INT_MAX constant

#### 2. Enhanced JSON Parser Logic
```cpp
// Skip lines without "fen" immediately
if (json_line.find("\"fen\"") == std::string::npos) {
    return entry;  // Don't process partial lines
}

// Find ALL "cp" values and track highest depth
while (cp_pos != std::string::npos && eval_count < 100) {
    // For each "cp", find its corresponding "depth"
    // Keep the evaluation with highest depth
    if (current_depth > best_depth) {
        best_depth = current_depth;
        best_cp = cp_val;
    }
}
```

#### 3. Set Depth to INT_MAX
```cpp
position.depth_of_eval = INT_MAX;  // Prioritize book entries
table.insert(position, INT_MAX);    // Over search results
```

#### 4. Cleaner Logging
- Removed warning for unparseable lines
- Progress shown every 5000 positions instead of 1000
- Silent failure for invalid FENs

### Updated: books/openings.json
Changed from multi-line JSON objects to **JSON Lines format** (one complete object per line):
```json
{"fen": "...", "evals": [{"pvs": [{"cp": 25, "line": "..."}], "depth": 36}]}
```

## Test Results

### Before Fix
```
Warning: Failed to parse line:       "depth": 26...
Warning: Failed to parse line:     }...
Warning: Failed to parse line:   ]...
Warning: Failed to parse line: }...
Successfully loaded 0 positions from 0 entries in Lichess JSON file.
```

### After Fix
```
Loading opening book from Lichess JSON: books/openings.json
Successfully loaded 5 positions from 5 entries in Lichess JSON file.
```

## Features

✅ **Multiple Evaluations**: Finds the best evaluation (highest depth) across all PVs
✅ **Negative Values**: Handles both positive and negative centipawn evaluations  
✅ **INT_MAX Depth**: All book entries use INT_MAX to prioritize over search
✅ **Robust Parsing**: Skips malformed lines silently
✅ **Progress Logging**: Shows loading progress for large files
✅ **Standard Format**: Works with standard Lichess JSON Lines format

## Usage

The engine now properly loads Lichess JSON files with the following format:

```json
{"fen": "starting_position_fen", "evals": [{"pvs": [{"cp": 20, "line": "moves..."}], "knodes": 206765, "depth": 36}]}
{"fen": "second_position_fen", "evals": [{"pvs": [{"cp": -25, "line": "moves..."}], "knodes": 192958, "depth": 34}]}
```

### Quick Test
```bash
# Build
g++ -fdiagnostics-color=always -g ascaniusfish.cpp -o ascaniusfish

# Run (test with sample JSON)
./ascaniusfish
```

Watch startup output for:
```
=== Loading Opening Book ===
Loading opening book from Lichess JSON: books/openings.json
Successfully loaded X positions from Y entries in Lichess JSON file.
=== Opening Book Loaded ===
```

## Configuration

All settings are at the top of `ascaniusfish.cpp`:

```cpp
const bool USE_OPENING_BOOK = true;          // Master toggle
const bool USE_LICHESS_JSON = true;          // Use JSON format (not PGN)
const std::string OPENING_BOOK_PATH = "books/openings.json";
const int OPENING_BOOK_MAX_POSITIONS = 10000; // Max positions to load
```

## What Each Position Gets

✓ **FEN**: Position in FEN notation (from JSON)
✓ **Evaluation**: Centipawn value with highest analysis depth
✓ **Depth**: INT_MAX (prioritizes over search)
✓ **Lookup Table**: Inserted for immediate lookup during play

Positions are now ready to be queried during engine search with pre-computed evaluations from Lichess database!
