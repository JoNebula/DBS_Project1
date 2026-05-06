CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc
SRC      := src/main.cpp
HDRS     := $(wildcard src/*.hpp)
BIN      := bin/btree_exp
DATA     := data/student.csv
RESULTS  := results
PLOTS    := $(RESULTS)/plots
VENV     := .venv
PY       := $(VENV)/bin/python

.PHONY: all run plots all-experiments venv clean distclean

all: $(BIN)

$(BIN): $(SRC) $(HDRS)
	@mkdir -p bin $(RESULTS)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

# Run the C++ experiments and write results/*.csv
run: $(BIN)
	@mkdir -p $(RESULTS)
	./$(BIN) $(DATA) $(RESULTS)

# Set up the Python virtualenv used by the plotting script
venv:
	@test -d $(VENV) || python3 -m venv $(VENV)
	@$(PY) -m pip install --quiet --upgrade pip
	@$(PY) -m pip install --quiet -r requirements.txt

# Render PNG plots from results/*.csv (assumes `make run` has been done)
plots: venv
	@mkdir -p $(PLOTS)
	$(PY) scripts/plot.py $(RESULTS)

# Run experiments, then render plots
all-experiments: run plots

clean:
	rm -rf bin $(RESULTS)/*.csv $(PLOTS)/*.png

distclean: clean
	rm -rf $(VENV)
