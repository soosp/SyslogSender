# Makefile — builds and runs the SyslogSender host tests.
#
# Requires a C++17 compiler (g++ or clang++) and GNU make. From this folder:
#   make            build and run every suite
#   make clean      remove the built binaries
#
# Native Windows cmd users: run  run_tests.bat  instead (no make needed).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O0 -Wall
INCLUDES := -I. -I../src

SUITES := format_harness parse_harness

.PHONY: all run clean $(SUITES:%=run-%)
all: run
run: $(SUITES:%=run-%)

%: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

run-%: %
	@echo "------------------------------------------------------------"
	@./$<

clean:
	@rm -f $(SUITES) $(SUITES:%=%.exe)
