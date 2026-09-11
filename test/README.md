# SyslogSender host tests

Run on a PC, no hardware. They exercise the shipping headers, never copies:

- **`format_harness.cpp`** — `src/SyslogFormat.h`: RFC 5424 header layout,
  PRI arithmetic, NILVALUEs, header-field sanitising and truncation, message
  truncation, and the timestamp's civil-date conversion against known
  instants (century leap rule, negative epochs).
- **`parse_harness.cpp`** — `src/SyslogEspLogParse.h`: the ESP-IDF and the
  Arduino log line formats, colour escapes, tags, and lines left alone.

| Environment | Command |
|---|---|
| VSCode | Ctrl+Shift+B → "Run SyslogSender tests" |
| Linux / macOS / WSL / Git-Bash | `./run_tests.sh` |
| Windows (cmd / double-click) | `run_tests.bat` |
| make | `make` |

Requires a C++17 compiler (`g++`/`clang++`, or MSYS2's g++ on Windows).
