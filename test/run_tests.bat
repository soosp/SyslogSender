@echo off
REM run_tests.bat - build and run the SyslogSender host tests on Windows.
REM Needs a C++17 g++ on PATH (MinGW-w64 / MSYS2). No make required.

cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
  echo ERROR: g++ not found on PATH.
  echo Install MinGW-w64 / MSYS2 and add its bin folder to PATH, then retry.
  echo.
  pause
  exit /b 1
)

set FLAGS=-std=c++17 -O0 -Wall -I. -I..\src
set RC=0

echo ------------------------------------------------------------
echo Building RFC 5424 framing ...
g++ %FLAGS% format_harness.cpp -o format_harness.exe
if errorlevel 1 ( echo BUILD FAILED: format_harness.cpp & set RC=1 & goto :done )
echo Running RFC 5424 framing:
format_harness.exe
if errorlevel 1 set RC=1

echo ------------------------------------------------------------
echo Building log line parsing ...
g++ %FLAGS% parse_harness.cpp -o parse_harness.exe
if errorlevel 1 ( echo BUILD FAILED: parse_harness.cpp & set RC=1 & goto :done )
echo Running log line parsing:
parse_harness.exe
if errorlevel 1 set RC=1

:done
echo ------------------------------------------------------------
if "%RC%"=="0" (
  echo ALL TESTS PASSED.
) else (
  echo SOME TESTS FAILED ^(see output above^).
)
echo.
if "%~1"=="" pause
exit /b %RC%
