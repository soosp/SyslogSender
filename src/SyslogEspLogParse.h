#pragma once

/**
 * @file SyslogEspLogParse.h
 * @brief Splits one ESP32 log line into level, tag and message.
 *
 * Pure C++ with no Arduino dependency, so test/parse_harness.cpp can run it on
 * a PC. Two line formats are recognised:
 *
 *   ESP-IDF   (build flag USE_ESP_IDF_LOG, the one esp_log_set_vprintf sees):
 *             I (1234) gps: module casic ...
 *             optionally wrapped in ANSI colour escapes when CONFIG_LOG_COLORS
 *             is set; those are skipped.
 *
 *   Arduino   (the core's ARDUHAL_LOG_FORMAT, for callers that feed such
 *             lines in by other means):
 *             [  1234][I][GpsReceiver.cpp:602] _taskBody(): [gps] module ...
 *             where a "[tag] " prefix on the message, if present, is taken
 *             as the tag and stripped.
 *
 * Source location and function name are not forwarded in either case: a
 * syslog line already carries host, app and time, and the location is a
 * debugging aid for the serial console.
 */

#include <cstddef>
#include <cstring>

namespace SyslogEspLogParse {

struct Parsed {
    char        level;     ///< 'E','W','I','D','V', or 0 if the line did not match
    const char* tag;       ///< Points into the line at the tag, or nullptr
    size_t      tagLen;    ///< Length of the tag
    const char* msg;       ///< Message text after the header (and tag), never null
    size_t      msgLen;    ///< Length of the message, excluding any trailing colour reset or newline
};

inline bool isLevel(char c) {
    return c == 'E' || c == 'W' || c == 'I' || c == 'D' || c == 'V';
}

/** @brief Skips one ANSI CSI sequence (ESC '[' ... final byte) at @p p. */
inline const char* skipAnsi(const char* p) {
    if (p[0] != '\033' || p[1] != '[') return p;
    p += 2;
    while (*p && !((*p >= '@' && *p <= '~'))) ++p;
    return *p ? p + 1 : p;
}

/** @brief Length of @p s without trailing CR/LF and a trailing ANSI reset. */
inline size_t trimmedLen(const char* s) {
    size_t n = strlen(s);
    for (;;) {
        while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) --n;
        // strip a trailing "\033[0m" (or any CSI ending in 'm')
        if (n >= 3 && s[n - 1] == 'm') {
            size_t k = n - 1;
            while (k > 0 && s[k] != '\033') { if (n - k > 8) break; --k; }
            if (s[k] == '\033' && k + 1 < n && s[k + 1] == '[') { n = k; continue; }
        }
        return n;
    }
}

/**
 * @brief Parses @p line in place. Never modifies it.
 *
 * A line in neither format is returned with level 0, no tag and msg == line,
 * so a caller can still forward it at a default severity.
 */
inline Parsed parse(const char* line) {
    Parsed p{0, nullptr, 0, line ? line : "", 0};
    if (!line) return p;
    p.msgLen = trimmedLen(line);

    const char* s = skipAnsi(line);

    // ESP-IDF: "X (digits) tag: msg"
    if (isLevel(s[0]) && s[1] == ' ' && s[2] == '(') {
        const char* q = s + 3;
        while (*q >= '0' && *q <= '9') ++q;
        if (q > s + 3 && q[0] == ')' && q[1] == ' ') {
            const char* tag = q + 2;
            const char* sep = strstr(tag, ": ");
            if (sep && sep > tag) {
                p.level  = s[0];
                p.tag    = tag;
                p.tagLen = static_cast<size_t>(sep - tag);
                p.msg    = sep + 2;
                p.msgLen = trimmedLen(p.msg);
                return p;
            }
        }
    }

    // Arduino: "[  1234][X][file:line] func(): msg"
    const char* h = strstr(s, "][");
    if (h && isLevel(h[2]) && h[3] == ']' && h[4] == '[') {
        p.level = h[2];
        const char* body = strstr(h + 5, "(): ");
        if (body) body += 4;
        else {
            body = strchr(h + 5, ']');
            body = body ? body + 1 : h + 5;
            while (*body == ' ') ++body;
        }
        p.msg = body;
        if (body[0] == '[') {
            const char* close = strchr(body + 1, ']');
            if (close && close > body + 1 && close - body - 1 <= 32 && close[1] == ' ') {
                bool plain = true;
                for (const char* c = body + 1; c < close; ++c) {
                    if (*c == ' ' || *c == '[') { plain = false; break; }
                }
                if (plain) {
                    p.tag    = body + 1;
                    p.tagLen = static_cast<size_t>(close - body - 1);
                    p.msg    = close + 2;
                }
            }
        }
        p.msgLen = trimmedLen(p.msg);
    }
    return p;
}

} // namespace SyslogEspLogParse
