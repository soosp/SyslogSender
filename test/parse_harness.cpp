// parse_harness.cpp — host test for splitting ESP32 log lines.
//
// Drives the REAL SyslogEspLogParse (src/SyslogEspLogParse.h) through both
// line formats it recognises and the lines it must leave alone.
//
// Build and run from this folder:
//   g++ -std=c++17 -O0 -Wall -I../src parse_harness.cpp -o parse_harness && ./parse_harness

#include <cstdio>
#include <string>

#include "SyslogEspLogParse.h"

namespace {
int g_failed = 0;
struct Case { const char* name; const char* line; char level; std::string tag; std::string msg; };
void run(const Case& c) {
    const auto p = SyslogEspLogParse::parse(c.line);
    std::string tag = p.tag ? std::string(p.tag, p.tagLen) : "";
    std::string msg(p.msg, p.msgLen);
    bool ok = p.level == c.level && tag == c.tag && msg == c.msg;
    std::printf("  [%s] %-46s level=%c tag='%s' msg='%s'\n", ok ? "PASS" : "FAIL", c.name,
                p.level ? p.level : '-', tag.c_str(), msg.c_str());
    if (!ok) { std::printf("       expected level=%c tag='%s' msg='%s'\n", c.level ? c.level : '-', c.tag.c_str(), c.msg.c_str()); ++g_failed; }
}
}

int main() {
    std::printf("SyslogEspLogParse host harness\n");
    const Case cases[] = {
        // ESP-IDF format (USE_ESP_IDF_LOG)
        { "IDF info line",             "I (1234) gps: module casic identified\n",         'I', "gps", "module casic identified" },
        { "IDF error, no newline",     "E (55) ntp: unexplained +1 s step",               'E', "ntp", "unexplained +1 s step" },
        { "IDF with colour escapes",   "\033[0;32mI (77) net: connected\033[0m\n",        'I', "net", "connected" },
        { "IDF message containing ': '", "W (9) gps: RING gap: 3 -> 5",                    'W', "gps", "RING gap: 3 -> 5" },
        { "IDF core tag",              "D (1) ARDUINO: uart init\n",                      'D', "ARDUINO", "uart init" },

        // Arduino ARDUHAL format
        { "Arduino line with [tag]",   "[  3967][I][GpsReceiver.cpp:2613] _casicIdentity(): [gps] CASIC $PCAS06: URANUS5\n", 'I', "gps", "CASIC $PCAS06: URANUS5" },
        { "Arduino line without tag",  "[ 10251][D][main.cpp:100] onGpsStatus(): status=acquiring sats=18\n",           'D', "",    "status=acquiring sats=18" },
        { "Arduino bracket not a tag", "[  1][W][x.cpp:1] f(): [not a tag] text\n",                                     'W', "",    "[not a tag] text" },
        { "Arduino line, no func",     "[  1][E][x.cpp:1] plain\n",                                                     'E', "",    "plain" },

        // Left alone
        { "unrecognised line",         "hello world\n",                                   0,   "",    "hello world" },
        { "empty line",                "",                                                0,   "",    "" },
    };
    for (const Case& c : cases) run(c);
    std::printf("%d case(s) failed.\n", g_failed);
    return g_failed ? 1 : 0;
}
