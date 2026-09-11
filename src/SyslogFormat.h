#pragma once

/**
 * @file SyslogFormat.h
 * @brief RFC 5424 message framing, severity mapping and timestamp formatting.
 *
 * Pure C++ with no Arduino dependency: SyslogSender uses it on the device and
 * test/format_harness.cpp runs the same code on a PC. Nothing here allocates;
 * every function writes into a caller-supplied buffer and truncates rather
 * than overflows.
 *
 * Frame produced (RFC 5424 §6):
 *
 *   <PRI>1 TIMESTAMP HOSTNAME APP-NAME PROCID MSGID - MSG
 *
 * PRI = facility * 8 + severity. Fields that are unknown carry the NILVALUE
 * "-". STRUCTURED-DATA is always "-". MSG is sent as-is (no BOM), which every
 * common receiver (rsyslog, syslog-ng, journald) accepts as ASCII/UTF-8.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace SyslogFormat {

/** @brief RFC 5424 severities. */
enum Severity : uint8_t {
    SEV_EMERGENCY = 0, SEV_ALERT = 1, SEV_CRITICAL = 2, SEV_ERROR = 3,
    SEV_WARNING = 4, SEV_NOTICE = 5, SEV_INFO = 6, SEV_DEBUG = 7,
};

/** @brief RFC 5424 facilities (the useful subset). */
enum Facility : uint8_t {
    FAC_KERN = 0, FAC_USER = 1, FAC_DAEMON = 3, FAC_SYSLOG = 5,
    FAC_LOCAL0 = 16, FAC_LOCAL1 = 17, FAC_LOCAL2 = 18, FAC_LOCAL3 = 19,
    FAC_LOCAL4 = 20, FAC_LOCAL5 = 21, FAC_LOCAL6 = 22, FAC_LOCAL7 = 23,
};

/** @brief Field length limits from RFC 5424 §6.2 (bytes, excluding NUL). */
constexpr size_t MAX_HOSTNAME = 255;
constexpr size_t MAX_APPNAME  = 48;
constexpr size_t MAX_PROCID   = 128;
constexpr size_t MAX_MSGID    = 32;

/**
 * @brief Copies a header field into @p out, replacing anything RFC 5424 does
 *        not allow in a header (space, control characters) with '_', and
 *        truncating to @p maxLen. Empty or null becomes the NILVALUE "-".
 * @return Characters written, excluding the NUL.
 */
inline size_t field(char* out, size_t cap, const char* in, size_t maxLen) {
    if (cap == 0) return 0;
    if (!in || !*in) {
        if (cap < 2) { out[0] = '\0'; return 0; }
        out[0] = '-'; out[1] = '\0';
        return 1;
    }
    size_t n = 0;
    while (in[n] && n < maxLen && n + 1 < cap) {
        const unsigned char c = static_cast<unsigned char>(in[n]);
        out[n] = (c <= 0x20 || c == 0x7F) ? '_' : static_cast<char>(c);
        ++n;
    }
    out[n] = '\0';
    return n;
}

/**
 * @brief Formats a UTC timestamp as RFC 3339 with microseconds:
 *        2026-09-11T06:23:41.123456Z. Needs 28 bytes including the NUL.
 *
 * Civil-date conversion is done here (Howard Hinnant's days-to-civil) rather
 * than through gmtime(), so it behaves identically on every platform,
 * including ones whose libc has no gmtime.
 *
 * @return Characters written, or 0 if @p cap is too small.
 */
inline size_t timestamp(char* out, size_t cap, int64_t epochSec, uint32_t usec) {
    if (cap < 28) { if (cap) out[0] = '\0'; return 0; }
    int64_t days = epochSec / 86400;
    int64_t rem  = epochSec % 86400;
    if (rem < 0) { rem += 86400; --days; }
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t doe = days - era * 146097;                                   // [0, 146096]
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    const int64_t y   = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               // [0, 365]
    const int64_t mp  = (5 * doy + 2) / 153;                                   // [0, 11]
    const int64_t d   = doy - (153 * mp + 2) / 5 + 1;                          // [1, 31]
    const int64_t m   = mp < 10 ? mp + 3 : mp - 9;                             // [1, 12]
    const int64_t yy  = y + (m <= 2 ? 1 : 0);
    const int hh = static_cast<int>(rem / 3600), mm = static_cast<int>((rem % 3600) / 60),
              ss = static_cast<int>(rem % 60);
    if (usec > 999999) usec = 999999;
    // int conversions on purpose: avr-libc's printf has no %lld, and a year
    // fits an int on every platform this runs on.
    const int n = snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d.%06luZ",
                           static_cast<int>(yy), static_cast<int>(m), static_cast<int>(d),
                           hh, mm, ss, (unsigned long)usec);
    return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

/**
 * @brief Builds one RFC 5424 message into @p out.
 *
 * @param out       Destination; always NUL-terminated on return (if cap > 0).
 * @param cap       Size of @p out.
 * @param facility  Facility code.
 * @param severity  Severity code.
 * @param timestamp RFC 3339 text from timestamp(), or nullptr/"" for NILVALUE.
 * @param hostname  Sender host name, or nullptr for NILVALUE.
 * @param app       APP-NAME, or nullptr for NILVALUE.
 * @param procid    PROCID, or nullptr for NILVALUE.
 * @param msgid     MSGID (a short tag such as "gps"), or nullptr for NILVALUE.
 * @param msg       Message text; a trailing newline is dropped.
 * @return Length of the message written (excluding the NUL). The message is
 *         truncated to fit; the header always fits when cap >= 96 and the
 *         fields are short.
 */
/**
 * @brief Builds only the header (through the "- " before MSG) into @p out, so
 *        a caller can format the message straight after it in one buffer.
 * @return Header length, or 0 if it did not fit (out is then empty).
 */
inline size_t buildHeader(char* out, size_t cap,
                          uint8_t facility, uint8_t severity,
                          const char* timestamp, const char* hostname, const char* app,
                          const char* procid, const char* msgid) {
    if (cap == 0) return 0;
    char host[MAX_HOSTNAME + 1], appn[MAX_APPNAME + 1], proc[MAX_PROCID + 1], mid[MAX_MSGID + 1];
    field(host, sizeof(host), hostname, MAX_HOSTNAME);
    field(appn, sizeof(appn), app,      MAX_APPNAME);
    field(proc, sizeof(proc), procid,   MAX_PROCID);
    field(mid,  sizeof(mid),  msgid,    MAX_MSGID);
    const unsigned pri = (facility & 0x1F) * 8u + (severity & 0x07);
    const char* ts = (timestamp && *timestamp) ? timestamp : "-";
    const int n = snprintf(out, cap, "<%u>1 %s %s %s %s %s - ", pri, ts, host, appn, proc, mid);
    if (n < 0 || static_cast<size_t>(n) >= cap) { out[0] = '\0'; return 0; }
    return static_cast<size_t>(n);
}

inline size_t build(char* out, size_t cap,
                    uint8_t facility, uint8_t severity,
                    const char* timestamp, const char* hostname, const char* app,
                    const char* procid, const char* msgid, const char* msg) {
    if (cap == 0) return 0;
    char host[MAX_HOSTNAME + 1], appn[MAX_APPNAME + 1], proc[MAX_PROCID + 1], mid[MAX_MSGID + 1];
    field(host, sizeof(host), hostname, MAX_HOSTNAME);
    field(appn, sizeof(appn), app,      MAX_APPNAME);
    field(proc, sizeof(proc), procid,   MAX_PROCID);
    field(mid,  sizeof(mid),  msgid,    MAX_MSGID);
    const unsigned pri = (facility & 0x1F) * 8u + (severity & 0x07);
    const char* ts = (timestamp && *timestamp) ? timestamp : "-";

    int n = snprintf(out, cap, "<%u>1 %s %s %s %s %s - ", pri, ts, host, appn, proc, mid);
    if (n < 0) { out[0] = '\0'; return 0; }
    size_t len = static_cast<size_t>(n);
    if (len >= cap) { out[cap - 1] = '\0'; return cap - 1; }

    if (msg) {
        size_t mlen = strlen(msg);
        while (mlen > 0 && (msg[mlen - 1] == '\n' || msg[mlen - 1] == '\r')) --mlen;
        if (mlen > cap - 1 - len) mlen = cap - 1 - len;
        memcpy(out + len, msg, mlen);
        len += mlen;
        out[len] = '\0';
    }
    return len;
}

/** @brief Maps the ESP-IDF / Arduino-ESP32 log level letter to a severity;
 *  unknown letters map to SEV_INFO. */
inline uint8_t severityFromEspLevel(char letter) {
    switch (letter) {
        case 'E': return SEV_ERROR;
        case 'W': return SEV_WARNING;
        case 'I': return SEV_INFO;
        case 'D': return SEV_DEBUG;
        case 'V': return SEV_DEBUG;
        default:  return SEV_INFO;
    }
}

} // namespace SyslogFormat
