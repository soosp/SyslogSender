#pragma once

/**
 * @file SyslogSender.h
 * @brief Sends RFC 5424 syslog messages over UDP from ESP32, ESP8266 and AVR.
 *
 * A sender, not a logging framework: something else decides what to log —
 * ESP_LOG (see SyslogEspLogHook.h), a library's logger callback, or the
 * application calling send() — and this class frames it and sends it. It
 * keeps no queue: a message is sent in the caller's context or dropped, so
 * logging never waits on the network and a lost UDP datagram is a lost
 * datagram. Serial output is somebody else's job and is never touched.
 *
 * Target is an IP or an FQDN, validated with Host's validators, resolved once
 * at begin() and again after resolveTarget() (call it on a network change).
 *
 * Thread safety (ESP32): send() may be called from any task; a mutex
 * serialises the UDP object, and a caller that cannot get it within
 * SYSLOG_SENDER_MUTEX_MS drops its message rather than waiting. Never call
 * from an ISR.
 *
 * Time: the timestamp comes from a caller-supplied clock (setClock()); with
 * none, or before the clock is valid, the NILVALUE is sent and the receiver
 * stamps the line on arrival.
 */

#include <Arduino.h>
#include <stdarg.h>
#include "SyslogFormat.h"

#if defined(ARDUINO_ARCH_ESP32)
#  include <esp_arduino_version.h>
#  if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#    include <Network.h>
#    include <NetworkUdp.h>
     using SyslogUdp = NetworkUDP;
#  else
#    include <WiFi.h>
#    include <WiFiUdp.h>
     using SyslogUdp = WiFiUDP;
#  endif
#  include <freertos/FreeRTOS.h>
#  include <freertos/semphr.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#  include <ESP8266WiFi.h>
#  include <WiFiUdp.h>
   using SyslogUdp = WiFiUDP;
#elif defined(ARDUINO_ARCH_AVR)
#  include <Ethernet.h>
#  include <EthernetUdp.h>
#  include <Dns.h>
   using SyslogUdp = EthernetUDP;
#else
#  error "SyslogSender: unsupported architecture"
#endif

#include <Host.h>

/** @brief Largest frame (header + message) sent in one datagram, and the
 *  stack a send() costs in the caller's context. RFC 5424 receivers must
 *  accept 480; 2048 is common. AVR defaults lower for its 2 kB of SRAM. */
#ifndef SYSLOG_SENDER_MAX_LEN
#  if defined(ARDUINO_ARCH_AVR)
#    define SYSLOG_SENDER_MAX_LEN 160
#  else
#    define SYSLOG_SENDER_MAX_LEN 512
#  endif
#endif

/** @brief How long (ms) a sender waits for the UDP mutex before dropping. */
#ifndef SYSLOG_SENDER_MUTEX_MS
#  define SYSLOG_SENDER_MUTEX_MS 20
#endif

class SyslogSender {
public:
    using Severity = SyslogFormat::Severity;
    using Facility = SyslogFormat::Facility;

    /** @brief Wall-clock source: sets @p epochSec and @p usec, returns false
     *  while the clock is not to be trusted (then no timestamp is sent). */
    using ClockFn = bool (*)(int64_t& epochSec, uint32_t& usec);

    SyslogSender();

    /**
     * @brief Configures the sender. Does not send and does not touch the
     *        network stack, so it is safe before the interface is up. The
     *        socket is opened by the first setEnabled(true); an FQDN target
     *        is resolved by resolveTarget(). Call both from the network-up
     *        event.
     * @param target   Syslog server, IPv4 dotted or FQDN (validated).
     * @param hostname HOSTNAME field: this device's name.
     * @param app      APP-NAME field.
     * @param port     UDP port, 514 by default.
     * @return false if @p target is neither a valid IP nor a valid FQDN.
     */
    bool begin(const char* target, const char* hostname, const char* app,
               uint16_t port = 514);

    /** @brief Stops sending; the configuration is kept. */
    void end();

    /** @brief Enables or disables sending. Enabling opens the UDP socket the
     *  first time, so it must be called with the network stack up. Disabled,
     *  send() returns false at once. Wire this to the network manager's
     *  connected/disconnected events. */
    void setEnabled(bool on);
    bool isEnabled() const { return _enabled; }

    /** @brief Lowest severity forwarded (numerically highest). Default DEBUG. */
    void setMinSeverity(uint8_t sev) { _minSeverity = sev; }
    uint8_t minSeverity() const { return _minSeverity; }

    /** @brief Facility for every message. Default LOCAL0. */
    void setFacility(uint8_t fac) { _facility = fac; }

    /** @brief PROCID field, e.g. a firmware version. Optional. */
    void setProcId(const char* procid);

    /** @brief Wall-clock source for the timestamp. Optional. */
    void setClock(ClockFn fn) { _clock = fn; }

    /**
     * @brief Resolves the target FQDN (parses an IP). Needs the network
     *        stack: call it from the network-up event, and again after a
     *        reconnect, since DNS may not have been reachable before.
     * @return true if the target is resolved.
     */
    bool resolveTarget();

    /** @brief Sends one message. @p msgid is a short tag (MSGID), may be null.
     *  @return true if handed to the network stack. */
    bool send(uint8_t severity, const char* msgid, const char* msg);

    /** @brief printf-style send(). */
    bool sendf(uint8_t severity, const char* msgid, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));
    bool vsendf(uint8_t severity, const char* msgid, const char* fmt, va_list ap);

    /**
     * @brief Flash-resident variants for AVR and ESP8266, where a PROGMEM
     *        string must not be read as a RAM pointer. On ESP32 they are the
     *        same as the RAM ones; F() and PSTR() work everywhere.
     */
    bool send(uint8_t severity, const char* msgid, const __FlashStringHelper* msg);
    bool sendf_P(uint8_t severity, const char* msgid, PGM_P fmt, ...);
    bool vsendf_P(uint8_t severity, const char* msgid, PGM_P fmt, va_list ap);

    /** @brief Messages dropped since begin(): disabled, unresolved, over
     *  size, or the mutex was busy. Diagnostic. */
    uint32_t dropped() const { return _dropped; }
    uint32_t sent() const { return _sent; }

    const char* target() const { return _target; }
    const IPAddress& targetIp() const { return _ip; }

private:
    size_t _header(char* frame, size_t cap, uint8_t severity, const char* msgid);
    bool _sendFrame(const char* frame, size_t len);
    bool _lock();
    void _unlock();

    char      _target[Host::MAX_FQDN_SIZE] = {0};
    char      _hostname[SyslogFormat::MAX_APPNAME * 2 + 1] = {0};
    char      _app[SyslogFormat::MAX_APPNAME + 1] = {0};
    char      _procid[SyslogFormat::MAX_PROCID + 1] = {0};
    IPAddress _ip;
    uint16_t  _port        = 514;
    bool      _targetIsIp  = false;
    bool      _resolved    = false;
    bool      _begun       = false;
    bool      _socketOpen  = false;
    volatile bool _enabled = false;
    uint8_t   _facility    = SyslogFormat::FAC_LOCAL0;
    uint8_t   _minSeverity = SyslogFormat::SEV_DEBUG;
    ClockFn   _clock       = nullptr;
    SyslogUdp _udp;
    uint32_t  _sent        = 0;
    uint32_t  _dropped     = 0;
#if defined(ARDUINO_ARCH_ESP32)
    SemaphoreHandle_t _mutex = nullptr;
#endif
};
