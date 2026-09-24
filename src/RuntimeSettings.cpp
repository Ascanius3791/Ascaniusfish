// OWNERSHIP=Claude
#include "../lib/RuntimeSettings.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace
{
    std::string trim(const std::string& s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    bool parse_bool(const std::string& raw, bool fallback)
    {
        std::string v = raw;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
        if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
        if (v == "false" || v == "0" || v == "no" || v == "off") return false;
        return fallback;
    }
}

RuntimeSettings load_runtime_settings(const std::string& path)
{
    RuntimeSettings settings; // defaults

    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Warning: could not open '" << path
                   << "', using default runtime settings (depth=" << settings.depth
                   << ", use_opening_book=" << settings.use_opening_book
                   << ", use_lookup_table=" << settings.use_lookup_table
                   << ", load_ptt=" << settings.load_ptt
                   << ", save_ptt=" << settings.save_ptt
                   << ", use_time_management=" << settings.use_time_management
                   << ", time_seconds=" << settings.time_seconds << ")" << std::endl;
        return settings;
    }

    std::string line;
    while (std::getline(file, line))
    {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;

        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });

        if (key == "depth") settings.depth = std::stoi(value);
        else if (key == "use_opening_book") settings.use_opening_book = parse_bool(value, settings.use_opening_book);
        else if (key == "use_lookup_table") settings.use_lookup_table = parse_bool(value, settings.use_lookup_table);
        else if (key == "load_ptt") settings.load_ptt = parse_bool(value, settings.load_ptt);
        else if (key == "save_ptt") settings.save_ptt = parse_bool(value, settings.save_ptt);
        else if (key == "use_time_management") settings.use_time_management = parse_bool(value, settings.use_time_management);
        else if (key == "time_seconds") settings.time_seconds = std::stod(value);
        else std::cerr << "Warning: unknown runtime_settings.txt key '" << key << "'" << std::endl;
    }

    return settings;
}
