# CS375 Lab 9 - build the simulator, tests and report
CXX      = g++
CXXFLAGS = -std=c++11 -Wall -Wextra
SRC      = src
BIN      = bin
PROGRAM  = $(BIN)/memory_simulator_advanced
TESTS    = $(BIN)/test_simulator
SUBMIT   = submission

all: $(PROGRAM)

$(PROGRAM): $(SRC)/memory_simulator_advanced.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(TESTS): tests/test_simulator.cpp $(SRC)/memory_simulator_advanced.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) -g -fsanitize=address,undefined -o $@ $<

$(BIN):
	mkdir -p $(BIN)

run: all
	./$(PROGRAM)

test: $(TESTS)
	./$(TESTS)

valgrind: all
	printf 'add 3 90000 5 RW\ntranslate 3 0 0 0 1\nrandom 200\nremove 3\nquit\n' | \
	  valgrind --leak-check=full --error-exitcode=1 ./$(PROGRAM) --config $(SRC)/segments.txt --seed 42 > /dev/null

report:
	cd report && latexmk -pdf report.tex

# Everything that goes on Canvas, in one folder
submission: report
	rm -rf $(SUBMIT)
	mkdir -p $(SUBMIT)
	cp $(SRC)/memory_simulator_advanced.cpp $(SRC)/segments.txt $(SRC)/addresses.txt report/report.pdf $(SUBMIT)/

clean:
	rm -rf $(BIN) *_results.txt memory_log.txt
	cd report && latexmk -c report.tex

.PHONY: all run test valgrind report submission clean
