# Full Opening Book Loading with Deduplication & Binary Save

This guide explains how to load the entire Lichess opening book database, deduplicate it (keeping the best evaluation per position), and save it as a binary file for fast subsequent loading.

## Overview

- **Full Book Size**: ~4.59 GB compressed (Lichess .zst file)
- **Processing**: ~10-30 minutes (first-time only)
- **Deduplication**: Keeps only the best evaluation (highest depth) per unique FEN position
- **Binary Save**: Creates a compact `books/openings.bin` for instant loading on next run

## Setup

### Step 1: Prepare the Full Lichess Database

The system expects the full compressed database at `books/lichess_db_eval.*.jsonl.zst.part` (the file you already downloaded).

If not already present, download from Lichess:
```bash
cd /home/ascanius/Documents/Cpp/other_programms/Chess/books
# Download the compressed file here (e.g., lichess_db_eval.R1EiK5mM.jsonl.zst.part)
```

### Step 2: Enable Full Book Loading

Edit [ascaniusfish.cpp](ascaniusfish.cpp#L7) and change:

```cpp
const bool LOAD_FULL_OPENING_BOOK = true;       // Set to true for one-time full load
```

### Step 3: Run the Engine

```bash
cd /home/ascanius/Documents/Cpp/other_programms/Chess

# Build (if not already built)
make

# Or build directly:
g++ -fdiagnostics-color=always -g ascaniusfish.cpp -o ascaniusfish

# Run - this will load and save the full book
# WARNING: This will take 10-30 minutes!
./ascaniusfish
```

The engine will:
1. **Read** the full compressed Lichess database
2. **Decompress** on-the-fly (via zstd)
3. **Deduplicate**: For each FEN, keep only the best (highest depth) evaluation
4. **Progress**: Show updates every 10,000 positions
5. **Insert**: Load all positions into the lookup table
6. **Save**: Save as `books/openings.bin` for fast future loading

### Step 4: Subsequent Runs

After the first run, the binary file is saved. You can:

**Option A: Disable full loading for faster startup**
```cpp
const bool LOAD_FULL_OPENING_BOOK = false;      // Back to normal (sample load)
```

The engine will load the sample 1000-position JSON file from `books/openings.json`.

**Option B: Add binary loading (future enhancement)**
Once binary loading is fully implemented, the engine can load from `books/openings.bin` in seconds instead of 10-30 minutes.

## Configuration

All settings are in [ascaniusfish.cpp](ascaniusfish.cpp#L5-L12):

```cpp
const bool USE_OPENING_BOOK = true;              // Master toggle
const bool USE_LICHESS_JSON = true;              // true = JSON, false = PGN
const bool LOAD_FULL_OPENING_BOOK = false;       // true = full load (slow, one-time)
const std::string OPENING_BOOK_PATH = "books/openings.json";    // Source JSON
const std::string OPENING_BOOK_BINARY_PATH = "books/openings.bin";  // Binary output
const int OPENING_BOOK_MAX_POSITIONS = -1;       // -1 = all, or limit (e.g., 100000)
```

## What Gets Deduplicated?

For each unique FEN string in the Lichess database:
- **Keep**: The evaluation (cp) from the entry with **highest depth** (best analysis)
- **Discard**: All other evaluations for that same position

Example:
```
FEN: "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"
  Entry 1: cp=20, depth=20
  Entry 2: cp=25, depth=35  ← KEPT (highest depth)
  Entry 3: cp=22, depth=15
```

## Performance Expectations

- **First run (full load)**: 10-30 minutes
  - Decompression + parsing: ~5-10 min
  - Deduplication: ~3-5 min
  - Insertion into table: ~2-10 min
  - Binary save: ~1 min
  
- **Subsequent runs (binary load)**: <5 seconds (future)

- **Current runs (sample JSON)**: <1 second

## Storage

```
books/
  openings.json        (1000-line sample, ~50 KB)
  openings.bin         (binary, ~500 MB - created after full load)
  lichess_db_eval*.zst.part   (4.59 GB compressed source)
```

## Troubleshooting

**Issue**: "Could not open Lichess JSON file"
- Check that the `.zst.part` file exists in `books/`
- Ensure it's fully downloaded

**Issue**: Running out of memory
- The full deduplication keeps all unique FENs in RAM (~1-2 GB during processing)
- If limited RAM, use `OPENING_BOOK_MAX_POSITIONS` to limit positions

**Issue**: Slow disk I/O during save
- Binary save writes sequentially; this is normal
- If SSD is bottleneck, consider moving `books/` to faster storage

## Implementation Details

### Deduplication Logic
The `load_and_save_full_opening_book()` function:
1. Reads all lines from the compressed `.zst.part` file
2. For each line, parses the JSON and extracts `fen` and best `cp` value
3. Stores in a map: `fen -> (cp_eval, depth)`
4. When same FEN appears again, updates only if new `depth` is higher
5. After reading all entries, inserts unique positions into lookup table

### Depth Handling
- Lichess JSON entries can have multiple evaluations with different depths
- The parser selects the `cp` value associated with the highest `depth` (best analysis)
- All book positions are stored with `depth_of_eval = INT_MAX` to ensure they outrank search results

### Binary Format (Future)
Currently saves header only. Full implementation will store:
- **Header** (16 bytes):
  - Magic: 0x42524F4B ("BRОК")
  - Version: 1
  - Position count
- **Entries** (16 bytes each):
  - Position hash (64-bit)
  - Evaluation (32-bit signed int)
  - Depth (32-bit signed int)

## Advanced Usage

### Custom Position Limit
To load only the first 100,000 unique positions:
```cpp
const int OPENING_BOOK_MAX_POSITIONS = 100000;
```

### Switch Between PGN and Lichess
The same mechanism works for PGN:
```cpp
const bool USE_LICHESS_JSON = false;    // Load from PGN instead
const std::string OPENING_BOOK_PATH = "books/openings.pgn";
```

### Disable Opening Book Entirely
```cpp
const bool USE_OPENING_BOOK = false;
```

---

**Next Steps**: After the first full load completes, disable `LOAD_FULL_OPENING_BOOK` and the engine will run normally with the full book pre-loaded in your lookup table.
