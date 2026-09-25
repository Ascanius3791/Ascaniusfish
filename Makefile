# OWNERSHIP=Claude
CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable -DNDEBUG

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
TOOL_TARGETS := tools/perft tools/bench tools/speed_compare
PROFILE_CXXFLAGS ?= -O2 -g -pg -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable

.PHONY: all run play asm tests debug profile-startpos perft bench speed-compare clean rebuild

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

tools/speed_compare: tools/speed_compare.cpp
	$(CXX) -O2 -Wall -o $@ tools/speed_compare.cpp

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

run play: $(TARGET)
	./$(TARGET)

debug: CXXFLAGS := -O0 -g -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
debug: $(TARGET)

rebuild: clean all

clean:
	rm -f $(TARGET) $(UCI_TARGET) a.out ascaniusfish.s $(TEST_TARGETS) $(TOOL_TARGETS) benchmarks/profile_startpos benchmarks/gmon.out benchmarks/profile_startpos.gprof
