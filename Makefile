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
PROFILE_CXXFLAGS ?= -O2 -g -pg -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable

.PHONY: all run play asm tests debug profile-startpos clean rebuild

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

run play: $(TARGET)
	./$(TARGET)

debug: CXXFLAGS := -O0 -g -Wall -Wno-unknown-pragmas -Wno-parentheses -Wno-unused-variable
debug: $(TARGET)

rebuild: clean all

clean:
	rm -f $(TARGET) $(UCI_TARGET) a.out ascaniusfish.s $(TEST_TARGETS) benchmarks/profile_startpos benchmarks/gmon.out benchmarks/profile_startpos.gprof
