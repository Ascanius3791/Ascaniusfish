    // OWNERSHIP=Claude
#ifndef RUNTIME_SETTINGS_HPP
#define RUNTIME_SETTINGS_HPP

#include <string>

// Values that used to be hardcoded consts in ascaniusfish.cpp, now editable
// in runtime_settings.txt without recompiling. Defaults here match the old
// hardcoded values and are used for any key missing from the file.
struct RuntimeSettings
{
    int depth = 6;
    bool use_opening_book = false;
    bool use_lookup_table = true;
    // Unused: the old in-memory PTT they were meant for is gone (#79). Still
    // parsed so runtime_settings.txt's keys stay valid.
    bool load_ptt = false;
    bool save_ptt = false;
    // Time management: when true, the engine picks its search depth via
    // iterative deepening against a per-move time budget (time_seconds)
    // instead of always searching to a fixed `depth`. See timed_engine_move()
    // in ascaniusfish_2.hpp.
    bool use_time_management = false;
    double time_seconds = 5.0;
};

// Parses "key=value" lines from path ('#' starts a comment, blank lines
// ignored). Unknown keys are ignored; missing keys keep their default.
// If the file can't be opened, prints a warning and returns the defaults.
RuntimeSettings load_runtime_settings(const std::string& path = "runtime_settings.txt");

#include "../src/RuntimeSettings.cpp"

#endif // RUNTIME_SETTINGS_HPP
