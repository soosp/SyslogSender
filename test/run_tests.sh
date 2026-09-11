#!/usr/bin/env sh
# run_tests.sh — build and run the SyslogSender host tests.
#
# Works on Linux, macOS, WSL, Git-Bash and MSYS2. Needs a C++17 compiler
# (g++ or clang++) on PATH. No make required.

cd "$(dirname "$0")" || exit 1

if   command -v g++      >/dev/null 2>&1; then CXX=g++
elif command -v clang++  >/dev/null 2>&1; then CXX=clang++
elif command -v c++      >/dev/null 2>&1; then CXX=c++
else
  echo "ERROR: no C++ compiler found (need g++, clang++ or c++ on PATH)."
  exit 1
fi

FLAGS="-std=c++17 -O0 -Wall -I. -I../src"

build_and_run() {
  src="$1"; out="$2"; label="$3"
  echo "------------------------------------------------------------"
  echo "Building $label ..."
  if ! $CXX $FLAGS "$src" -o "$out"; then
    echo "BUILD FAILED: $src"
    return 1
  fi
  echo "Running $label:"
  "./$out"
  return $?
}

rc=0
build_and_run format_harness.cpp format_harness "RFC 5424 framing" || rc=1
build_and_run parse_harness.cpp  parse_harness  "log line parsing"  || rc=1

echo "------------------------------------------------------------"
if [ "$rc" -eq 0 ]; then echo "ALL TESTS PASSED."; else echo "SOME TESTS FAILED (see output above)."; fi
exit "$rc"
