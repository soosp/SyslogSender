// format_harness.cpp — host test for RFC 5424 framing and the timestamp.
//
// Drives the REAL SyslogFormat (src/SyslogFormat.h, the code the device
// runs): header layout, PRI arithmetic, NILVALUEs, field sanitising and
// truncation, trailing-newline handling, and the civil-date conversion
// against known instants (leap years, century rule, negative epochs).
//
// Build and run from this folder:
//   g++ -std=c++17 -O0 -Wall -I../src format_harness.cpp -o format_harness && ./format_harness

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "SyslogFormat.h"

using namespace SyslogFormat;

namespace {
int g_failed = 0;
void check(bool ok, const char* name, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) ++g_failed;
}
std::string ts(int64_t sec, uint32_t usec) {
    char b[32]; timestamp(b, sizeof(b), sec, usec); return b;
}
}

int main() {
    std::printf("SyslogFormat host harness\n");
    char out[512];

    // --- timestamp ---------------------------------------------------------
    check(ts(0, 0) == "1970-01-01T00:00:00.000000Z", "epoch zero", ts(0, 0));
    check(ts(1789107821, 123456) == "2026-09-11T06:23:41.123456Z", "2026-09-11 06:23:41.123456", ts(1789107821, 123456));
    check(ts(951782400, 0) == "2000-02-29T00:00:00.000000Z", "2000-02-29 (century leap day)", ts(951782400, 0));
    check(ts(4107542400LL, 0) == "2100-03-01T00:00:00.000000Z", "2100-03-01 (2100 is not leap)", ts(4107542400LL, 0));
    check(ts(-1, 999999) == "1969-12-31T23:59:59.999999Z", "one microsecond before the epoch", ts(-1, 999999));
    check(ts(1483228800, 0) == "2017-01-01T00:00:00.000000Z", "after the last leap second", ts(1483228800, 0));
    check(ts(1, 1000000) == "1970-01-01T00:00:01.999999Z", "usec clamped to 999999", ts(1, 1000000));
    { char small[20]; check(timestamp(small, sizeof(small), 0, 0) == 0 && small[0] == '\0', "too small a buffer writes nothing"); }

    // --- PRI and header layout ---------------------------------------------
    build(out, sizeof(out), FAC_LOCAL0, SEV_INFO, "2026-09-11T06:23:41.123456Z", "yas1-123", "yas1", "0.1.0", "gps", "module casic");
    check(std::string(out) == "<134>1 2026-09-11T06:23:41.123456Z yas1-123 yas1 0.1.0 gps - module casic", "local0.info full header", out);
    build(out, sizeof(out), FAC_USER, SEV_ERROR, nullptr, nullptr, nullptr, nullptr, nullptr, "x");
    check(std::string(out) == "<11>1 - - - - - - x", "user.error, every field NILVALUE", out);
    build(out, sizeof(out), FAC_LOCAL7, SEV_DEBUG, "", "h", "a", "", "", "m");
    check(std::string(out) == "<191>1 - h a - - - m", "empty strings are NILVALUE", out);
    check(FAC_LOCAL7 * 8 + SEV_DEBUG == 191 && FAC_KERN * 8 + SEV_EMERGENCY == 0, "PRI arithmetic bounds");

    // --- field sanitising ----------------------------------------------------
    build(out, sizeof(out), FAC_LOCAL0, SEV_INFO, nullptr, "my host", "ap p\tx", nullptr, "t g", "m");
    check(std::string(out) == "<134>1 - my_host ap_p_x - t_g - m", "spaces and controls in header fields become '_'", out);
    {
        std::string longapp(60, 'a');
        build(out, sizeof(out), FAC_LOCAL0, SEV_INFO, nullptr, "h", longapp.c_str(), nullptr, nullptr, "m");
        check(strstr(out, std::string(48, 'a').c_str()) && !strstr(out, std::string(49, 'a').c_str()), "APP-NAME truncated to 48");
    }

    // --- message handling ----------------------------------------------------
    build(out, sizeof(out), FAC_LOCAL0, SEV_INFO, nullptr, "h", "a", nullptr, "t", "line with newline\r\n");
    check(std::string(out) == "<134>1 - h a - t - line with newline", "trailing CR/LF dropped", out);
    build(out, sizeof(out), FAC_LOCAL0, SEV_INFO, nullptr, "h", "a", nullptr, "t", "");
    check(std::string(out) == "<134>1 - h a - t - ", "empty message keeps the separator", out);
    {
        char tiny[40];
        std::string longmsg(200, 'x');
        size_t n = build(tiny, sizeof(tiny), FAC_LOCAL0, SEV_INFO, nullptr, "h", "a", nullptr, "t", longmsg.c_str());
        check(n == sizeof(tiny) - 1 && tiny[n] == '\0' && strncmp(tiny, "<134>1 - h a - t - ", 19) == 0, "message truncated to the buffer, header intact", tiny);
    }
    {
        char none[1];
        check(build(none, 0, FAC_LOCAL0, SEV_INFO, nullptr, "h", "a", nullptr, "t", "m") == 0, "zero capacity writes nothing");
    }

    // --- header-only builder -------------------------------------------------
    {
        char h[96];
        size_t n = buildHeader(h, sizeof(h), FAC_LOCAL0, SEV_NOTICE, nullptr, "h", "a", nullptr, "t");
        check(n == strlen("<133>1 - h a - t - ") && std::string(h) == "<133>1 - h a - t - ", "buildHeader ends with the MSG separator", h);
        char tiny[8];
        check(buildHeader(tiny, sizeof(tiny), FAC_LOCAL0, SEV_NOTICE, nullptr, "h", "a", nullptr, "t") == 0 && tiny[0] == '\0', "buildHeader that does not fit writes nothing");
    }

    // --- severity mapping ----------------------------------------------------
    check(severityFromEspLevel('E') == SEV_ERROR && severityFromEspLevel('W') == SEV_WARNING &&
          severityFromEspLevel('I') == SEV_INFO && severityFromEspLevel('D') == SEV_DEBUG &&
          severityFromEspLevel('V') == SEV_DEBUG && severityFromEspLevel('?') == SEV_INFO,
          "ESP level letters map to severities");

    std::printf("%d check(s) failed.\n", g_failed);
    return g_failed ? 1 : 0;
}
