# OWNERSHIP=Claude
CXX ?= g++
# -mpopcnt: count bits with the CPU instruction (every x86-64 CPU since 2008) instead of
# a libgcc call; ~26% fewer search instructions. tools/match.cpp and speed_compare.cpp match it.
# -fwhole-program: every binary is one translation unit, so functions it never calls are
# dropped before they are optimised; ~15-25% less compile time, same bench, same nps.
CXXFLAGS ?= -O3 -mpopcnt -fwhole-program -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG

TARGET ?= ascaniusfish
MAIN := ascaniusfish.cpp
UCI_TARGET := ascaniusfish_uci
HEADERS := $(wildcard *.hpp lib/*.hpp)
SOURCES := $(wildcard src/*.cpp)
TEST_SOURCES := hash_table_test.cpp hash_game_test.cpp pv_first_move_diagnostic.cpp
TEST_TARGETS := hash_table_test hash_game_test pv_first_move_diagnostic
PROFILE_ITERATIONS ?= 1
PROFILE_DEPTH ?= 100000
PERFT_DEPTH ?=
BENCH_DEPTH ?=
ROUNDS ?= 10
DEPTH ?=
TC ?=
CONCURRENCY ?=
PAIRS ?=
MOVETIME ?= 10000
GAMES ?=
MOVETIME_TT ?=
PLIES ?=
TT_EXPONENT ?=
GUI_PORT ?=
GUI_BIND ?=
GUI_TUNNEL ?=
SYZYGY_PATH ?= $(HOME)/syzygy-nr
SYZYGY_RANDOM ?=
TOOL_TARGETS := tools/perft tools/bench tools/speed_compare tools/match tools/make_openings tools/gui_match tools/tt_stats tools/syzygy_reference tools/tb_suite tools/tb_trade_suite
PROFILE_CXXFLAGS ?= -O2 -mpopcnt -g -pg -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable

.PHONY: all run play asm tests debug profile-startpos perft bench speed-compare match gui-match gui tt-stats syzygy-test tb-suite tb-trade-suite clean rebuild

all: $(TARGET) $(UCI_TARGET)

$(TARGET): $(MAIN) $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $(MAIN)

# UCI engine (stdin/stdout protocol), separate binary so GUIs can launch it without arguments
$(UCI_TARGET): ascaniusfish_uci.cpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ ascaniusfish_uci.cpp

a.out: $(MAIN) $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $(MAIN)

asm: ascaniusfish.s

ascaniusfish.s: $(MAIN) $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -S -o $@ $(MAIN)

tests: $(TEST_TARGETS)

hash_table_test: hash_table_test.cpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ hash_table_test.cpp

hash_game_test: hash_game_test.cpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ hash_game_test.cpp

pv_first_move_diagnostic: pv_first_move_diagnostic.cpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ pv_first_move_diagnostic.cpp

benchmarks/profile_startpos: benchmarks/profile_startpos.cpp $(HEADERS) $(SOURCES)
	$(CXX) $(PROFILE_CXXFLAGS) -o $@ benchmarks/profile_startpos.cpp

profile-startpos: benchmarks/profile_startpos
	cd benchmarks && ./profile_startpos $(PROFILE_ITERATIONS) $(PROFILE_DEPTH)
	cd benchmarks && gprof ./profile_startpos gmon.out > profile_startpos.gprof
	@echo "Wrote benchmarks/profile_startpos.gprof"

tools/perft: tools/perft.cpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ tools/perft.cpp

tools/bench: tools/bench.cpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ tools/bench.cpp

tools/speed_compare: tools/speed_compare.cpp tools/git_build.hpp
	$(CXX) -O2 -Wall -o $@ tools/speed_compare.cpp

tools/match: tools/match.cpp tools/game_rules.hpp tools/git_build.hpp tools/uci_engine.hpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ tools/match.cpp

tools/gui_match: tools/gui_match.cpp tools/game_rules.hpp tools/uci_engine.hpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ tools/gui_match.cpp

tools/make_openings: tools/make_openings.cpp tools/game_rules.hpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ tools/make_openings.cpp

tools/syzygy_reference: tools/syzygy_reference.cpp tools/syzygy_positions.hpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ tools/syzygy_reference.cpp

tools/tb_suite: tools/tb_suite.cpp tools/syzygy_positions.hpp tools/game_rules.hpp tools/uci_engine.hpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ tools/tb_suite.cpp

# Endgame games with the tablebases (issue #38): the engine has to win what is
# won inside the 50-move rule and not lose what is drawn. TB_MOVETIME in ms.
TB_MOVETIME ?=
tb-suite: $(UCI_TARGET) tools/tb_suite
	./tools/tb_suite run $(SYZYGY_PATH) movetime=$(TB_MOVETIME)

tools/tb_trade_suite: tools/tb_trade_suite.cpp tools/syzygy_positions.hpp tools/uci_engine.hpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ tools/tb_trade_suite.cpp

# 6-7 piece positions where the right trade decides the result (issue #39): the
# solve count with the tables (TB_TRADE_ARGS=tables=off for the baseline).
TB_TRADE_ARGS ?=
tb-trade-suite: $(UCI_TARGET) tools/tb_trade_suite
	./tools/tb_trade_suite run $(SYZYGY_PATH) $(TB_TRADE_ARGS)

# Browser GUI server (issues #14-#17). Plain g++, no Node: gui/web/vendor holds
# a prebuilt chessground bundle, and the HTTP/SSE server is gui/http_server.hpp.
GUI_TARGET := gui/ascaniusfish_gui
GUI_HEADERS := gui/http_server.hpp gui/session.hpp gui/move_tree.hpp gui/json.hpp gui/engine_link.hpp gui/analysis_store.hpp gui/tablebase_view.hpp

$(GUI_TARGET): gui/gui_server.cpp $(GUI_HEADERS) tools/game_rules.hpp tools/uci_engine.hpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ gui/gui_server.cpp

# Syzygy prober (lib/syzygy.hpp) vs the recorded Lichess reference, plus a
# consistency check over SYZYGY_RANDOM (300) random positions per table
diagnostics/syzygy_test: diagnostics/syzygy_test.cpp tools/syzygy_positions.hpp $(HEADERS) $(SOURCES)
	$(CXX) $(CXXFLAGS) -pthread -o $@ diagnostics/syzygy_test.cpp

syzygy-test: diagnostics/syzygy_test
	./diagnostics/syzygy_test $(SYZYGY_PATH) $(SYZYGY_RANDOM)

# Move generation vs known perft counts (PERFT_DEPTH=4 for a quick check)
perft: tools/perft
	./tools/perft $(PERFT_DEPTH)

# Fixed-depth search; total nodes = search signature (BENCH_DEPTH overrides)
bench: tools/bench
	./tools/bench $(BENCH_DEPTH)

# Speed A/B of two git refs ("." = working tree): make speed-compare A=main B=.
speed-compare: tools/speed_compare
	@test -n "$(A)" -a -n "$(B)" || (echo "usage: make speed-compare A=<ref> B=<ref> [ROUNDS=10] [BENCH_DEPTH=n]"; exit 2)
	./tools/speed_compare $(A) $(B) $(ROUNDS) $(BENCH_DEPTH)

# Elo match of two engines (git refs or UCI binaries): make match A=main B=.
match: tools/match
	@test -n "$(A)" -a -n "$(B)" || (echo "usage: make match A=<ref|binary> B=<ref|binary> [DEPTH=3] [TC=10+0.1] [CONCURRENCY=n] [PAIRS=n] [OPTIONS_A=Name=Value,...] [OPTIONS_B=...]"; exit 2)
	./tools/match $(A) $(B) depth=$(DEPTH) tc=$(TC) concurrency=$(CONCURRENCY) pairs=$(PAIRS) optionsA="$(OPTIONS_A)" optionsB="$(OPTIONS_B)"

# One game of two UCI binaries at MOVETIME ms per move, shown in display_board.py
gui-match: tools/gui_match
	@test -n "$(A)" -a -n "$(B)" || (echo "usage: make gui-match A=<white binary> B=<black binary> [MOVETIME=10000]"; exit 2)
	./tools/gui_match $(A) $(B) movetime=$(MOVETIME)

# The board in the browser: prints a http://localhost:<port> URL and serves it.
# Play mode drives $(UCI_TARGET) over pipes, so that has to exist too.
gui: $(GUI_TARGET) $(UCI_TARGET)
	./$(GUI_TARGET) $(if $(GUI_PORT),port=$(GUI_PORT)) $(if $(GUI_BIND),bind=$(GUI_BIND)) $(if $(GUI_TUNNEL),tunnel=$(GUI_TUNNEL)) $(if $(SYZYGY),syzygy=$(SYZYGY))

# make gui plus a cloudflared quick tunnel, so the printed link is already
# shareable — no separate terminal, no combining a token by hand (issue #27).
gui-remote: $(GUI_TARGET) $(UCI_TARGET)
	./$(GUI_TARGET) $(if $(GUI_PORT),port=$(GUI_PORT)) tunnel=cloudflared

# TT discard statistics over self-play games (issue #11). The only build with
# -DTT_STATS; rebuilt every time so TT_EXPONENT=n always takes effect.
tt-stats:
	$(CXX) $(CXXFLAGS) -DTT_STATS $(if $(TT_EXPONENT),-DTT_EXPONENT=$(TT_EXPONENT)) -pthread -o tools/tt_stats tools/tt_stats.cpp
	./tools/tt_stats depth=$(DEPTH) movetime=$(MOVETIME_TT) games=$(GAMES) plies=$(PLIES)

run play: $(TARGET)
	./$(TARGET)

debug: CXXFLAGS := -O0 -mpopcnt -g -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
debug: $(TARGET)

rebuild: clean all

clean:
	rm -f $(TARGET) $(UCI_TARGET) a.out ascaniusfish.s $(TEST_TARGETS) $(TOOL_TARGETS) diagnostics/syzygy_test $(GUI_TARGET) benchmarks/profile_startpos benchmarks/gmon.out benchmarks/profile_startpos.gprof
