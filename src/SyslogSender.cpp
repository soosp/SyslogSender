#include "SyslogSender.h"

SyslogSender::SyslogSender() {
#if defined(ARDUINO_ARCH_ESP32)
    _mutex = xSemaphoreCreateMutex();
#endif
}

bool SyslogSender::begin(const char* target, const char* hostname, const char* app,
                         uint16_t port) {
    if (!target || !*target) return false;
    const bool isIp = Host::isValidIp(target);
    if (!isIp && !Host::isValidFqdn(target)) return false;

    strncpy(_target, target, sizeof(_target) - 1);
    _target[sizeof(_target) - 1] = '\0';
    strncpy(_hostname, hostname ? hostname : "", sizeof(_hostname) - 1);
    _hostname[sizeof(_hostname) - 1] = '\0';
    strncpy(_app, app ? app : "", sizeof(_app) - 1);
    _app[sizeof(_app) - 1] = '\0';
    _port       = port;
    _targetIsIp = isIp;
    _resolved   = false;
    _sent = _dropped = 0;
    _begun = true;

    // Nothing touches the network stack here: begin() may run before it
    // exists (setup() typically configures before the interface is up), and
    // opening a socket then asserts inside lwIP. An IP target is parsed now;
    // an FQDN waits for resolveTarget(), and the socket for setEnabled(true).
    if (_targetIsIp) resolveTarget();
    return true;
}

void SyslogSender::end() {
    _enabled = false;
    if (_socketOpen) { _udp.stop(); _socketOpen = false; }
    _begun = false;
}

void SyslogSender::setEnabled(bool on) {
    if (!_begun) { _enabled = false; return; }
    if (on && !_socketOpen) {
        // Local port 0 lets the stack pick one; needed for beginPacket() on
        // every core. Called from the network-up event, when the stack is up.
        _socketOpen = _udp.begin(0) == 1;
    }
    _enabled = on && _socketOpen;
}

void SyslogSender::setProcId(const char* procid) {
    strncpy(_procid, procid ? procid : "", sizeof(_procid) - 1);
    _procid[sizeof(_procid) - 1] = '\0';
}

bool SyslogSender::resolveTarget() {
    if (!_begun) return false;
    if (_targetIsIp) {
        uint8_t o[4];
        if (!Host::parseIp(_target, o)) return false;
        _ip = IPAddress(o[0], o[1], o[2], o[3]);
        _resolved = true;
        return true;
    }
    IPAddress ip;
    bool ok = false;
#if defined(ARDUINO_ARCH_ESP32) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ok = Network.hostByName(_target, ip) == 1;   // any interface: WiFi, Ethernet, PPP
#elif defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    ok = WiFi.hostByName(_target, ip) == 1;
#elif defined(ARDUINO_ARCH_AVR)
    DNSClient dns;
    dns.begin(Ethernet.dnsServerIP());
    ok = dns.getHostByName(_target, ip) == 1;
#endif
    if (ok && ip != IPAddress(0, 0, 0, 0)) {
        _ip = ip;
        _resolved = true;
    }
    return _resolved;
}

bool SyslogSender::_lock() {
#if defined(ARDUINO_ARCH_ESP32)
    return _mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(SYSLOG_SENDER_MUTEX_MS)) == pdTRUE;
#else
    return true;
#endif
}

void SyslogSender::_unlock() {
#if defined(ARDUINO_ARCH_ESP32)
    if (_mutex) xSemaphoreGive(_mutex);
#endif
}

size_t SyslogSender::_header(char* frame, size_t cap, uint8_t severity, const char* msgid) {
    char ts[32];
    ts[0] = '\0';
    if (_clock) {
        int64_t sec; uint32_t usec;
        if (_clock(sec, usec)) SyslogFormat::timestamp(ts, sizeof(ts), sec, usec);
    }
    return SyslogFormat::buildHeader(frame, cap, _facility, severity,
                                     ts, _hostname, _app, _procid, msgid);
}

bool SyslogSender::send(uint8_t severity, const char* msgid, const char* msg) {
    if (!_enabled || !_resolved || (severity & 0x07) > _minSeverity) {
        if (_enabled) ++_dropped;
        return false;
    }
    char frame[SYSLOG_SENDER_MAX_LEN];
    size_t len = _header(frame, sizeof(frame), severity, msgid);
    if (len == 0) { ++_dropped; return false; }
    if (msg) {
        size_t mlen = strlen(msg);
        while (mlen > 0 && (msg[mlen - 1] == '\n' || msg[mlen - 1] == '\r')) --mlen;
        if (mlen > sizeof(frame) - 1 - len) mlen = sizeof(frame) - 1 - len;
        memcpy(frame + len, msg, mlen);
        len += mlen;
    }
    frame[len] = '\0';
    return _sendFrame(frame, len);
}

bool SyslogSender::sendf(uint8_t severity, const char* msgid, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const bool ok = vsendf(severity, msgid, fmt, ap);
    va_end(ap);
    return ok;
}

bool SyslogSender::vsendf(uint8_t severity, const char* msgid, const char* fmt, va_list ap) {
    if (!_enabled || !_resolved || (severity & 0x07) > _minSeverity) {
        if (_enabled) ++_dropped;
        return false;
    }
    // One buffer: the message is formatted straight after the header.
    char frame[SYSLOG_SENDER_MAX_LEN];
    size_t len = _header(frame, sizeof(frame), severity, msgid);
    if (len == 0) { ++_dropped; return false; }
    const int n = vsnprintf(frame + len, sizeof(frame) - len, fmt, ap);
    if (n < 0) { ++_dropped; return false; }
    len += (static_cast<size_t>(n) < sizeof(frame) - len) ? static_cast<size_t>(n)
                                                          : sizeof(frame) - 1 - len;
    while (len > 0 && (frame[len - 1] == '\n' || frame[len - 1] == '\r')) --len;
    frame[len] = '\0';
    return _sendFrame(frame, len);
}

bool SyslogSender::send(uint8_t severity, const char* msgid, const __FlashStringHelper* msg) {
    if (!_enabled || !_resolved || (severity & 0x07) > _minSeverity) {
        if (_enabled) ++_dropped;
        return false;
    }
    char frame[SYSLOG_SENDER_MAX_LEN];
    size_t len = _header(frame, sizeof(frame), severity, msgid);
    if (len == 0) { ++_dropped; return false; }
    if (msg) {
        // Copy from flash straight after the header; truncate to fit.
        PGM_P src = reinterpret_cast<PGM_P>(msg);
        size_t room = sizeof(frame) - 1 - len;
        size_t n = 0;
        for (;;) {
            const char c = static_cast<char>(pgm_read_byte(src + n));
            if (c == '\0' || n >= room) break;
            frame[len + n] = c;
            ++n;
        }
        while (n > 0 && (frame[len + n - 1] == '\n' || frame[len + n - 1] == '\r')) --n;
        len += n;
    }
    frame[len] = '\0';
    return _sendFrame(frame, len);
}

bool SyslogSender::sendf_P(uint8_t severity, const char* msgid, PGM_P fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const bool ok = vsendf_P(severity, msgid, fmt, ap);
    va_end(ap);
    return ok;
}

bool SyslogSender::vsendf_P(uint8_t severity, const char* msgid, PGM_P fmt, va_list ap) {
    if (!_enabled || !_resolved || (severity & 0x07) > _minSeverity) {
        if (_enabled) ++_dropped;
        return false;
    }
    char frame[SYSLOG_SENDER_MAX_LEN];
    size_t len = _header(frame, sizeof(frame), severity, msgid);
    if (len == 0) { ++_dropped; return false; }
    const int n = vsnprintf_P(frame + len, sizeof(frame) - len, fmt, ap);
    if (n < 0) { ++_dropped; return false; }
    len += (static_cast<size_t>(n) < sizeof(frame) - len) ? static_cast<size_t>(n)
                                                          : sizeof(frame) - 1 - len;
    while (len > 0 && (frame[len - 1] == '\n' || frame[len - 1] == '\r')) --len;
    frame[len] = '\0';
    return _sendFrame(frame, len);
}

bool SyslogSender::_sendFrame(const char* frame, size_t len) {
    if (!_lock()) { ++_dropped; return false; }
    bool ok = _udp.beginPacket(_ip, _port) == 1;
    if (ok) {
        _udp.write(reinterpret_cast<const uint8_t*>(frame), len);
        ok = _udp.endPacket() == 1;
    }
    _unlock();
    if (ok) ++_sent; else ++_dropped;
    return ok;
}
