# SyslogSender

An Arduino library that sends RFC 5424 syslog messages over UDP from ESP32,
ESP8266 and AVR. It is a **sender, not a logging framework**: something else
decides what to log — `ESP_LOGx` on ESP32, a library's logger callback, or the
application — and this class frames it and sends it.

```cpp
SyslogSender syslog;
syslog.begin("192.168.1.10", "esp-123456", "myapp"); // target (IP or FQDN), HOSTNAME, APP-NAME
syslog.setEnabled(true);                             // wire to your network manager's events
syslog.send(SyslogFormat::SEV_WARNING, "gps", "PPS lost");
```

On the wire:

```txt
<132>1 2026-09-11T06:23:41.123456Z esp-123456 myapp - gps - PPS lost
```

## What it does, and does not

- **No queue.** A message is sent in the caller's context or dropped. Logging
  never waits on the network; a lost datagram is a lost datagram, and the
  `dropped()` counter says how many.
- **No heap after `begin()`.** One frame buffer on the caller's stack
  (`SYSLOG_SENDER_MAX_LEN`, 512 by default).
- **No serial.** Console output stays whatever it was; the ESP_LOG hook
  forwards a copy and leaves the console line untouched.
- **Thread-safe on ESP32.** `send()` may be called from any task; the UDP
  object is behind a mutex with a short timeout (`SYSLOG_SENDER_MUTEX_MS`), and
  a caller that cannot get it drops rather than waits. Never call from an ISR.
- **Timestamps from your clock.** `setClock()` takes a function that says
  whether the wall clock is to be trusted and what it reads; until it is, the
  NILVALUE is sent and the receiver stamps the line on arrival.
- **Target validated, not resolved on every send.** IP or FQDN, checked with
  [Host](https://github.com/soosp/Host)'s validators, resolved once at
  `begin()` and again on `resolveTarget()` — call that when the network comes
  back, since DNS may not have been reachable at boot.

Flash-resident text on AVR and ESP8266: `send(sev, tag, F("..."))` and
`sendf_P(sev, tag, PSTR("..."), ...)` read the string from PROGMEM; the plain
`send()`/`sendf()` take RAM pointers. On ESP32 the two are interchangeable.

## Forwarding ESP_LOG on ESP32

```cpp
#include <SyslogEspLogHook.h>
SyslogEspLogHook::install(syslog, SyslogFormat::SEV_INFO);   // E, W, I forwarded; D, V console-only
```

**Build with `-DUSE_ESP_IDF_LOG`.** The Arduino core's default logging
(`ESP_LOGx` redefined to `log_printf`, the `[ms][X][file:line] func():` lines)
writes straight to the ROM console and never passes through
`esp_log_set_vprintf()`, which is the only hook there is. With
`USE_ESP_IDF_LOG` both the core and the application log through ESP-IDF in its
`X (ms) tag: msg` format, every line reaches the hook, and the tag becomes the
syslog MSGID. Set the runtime level too — `esp_log_level_set("*",
ESP_LOG_DEBUG)` — because ESP-IDF defaults to INFO.

The hook is reentrancy-safe (a log line emitted by the network stack while
sending stays console-only) and ignores lines logged from an ISR.

## Feeding a library's logger

Libraries such as [AsyncConfigPortal](https://github.com/soosp/AsyncConfigPortal)
expose `setLogger(fn)` rather than a transport. Adapt the callback:

```cpp
portal.setLogger([](AsyncConfigPortal::LogLevel level, const char* tag, const char* msg) {
    syslog.send(severityOf(level), tag, msg);
});
```

See `examples/PortalLogger`.

## API

All methods are on `SyslogSender`; severities and facilities are the
`SyslogFormat::SEV_*` and `SyslogFormat::FAC_*` enumerators.

### Configuration

|Method|Meaning|
|---|---|
|`bool begin(target, hostname, app, port = 514)`|Validates and stores the target (IPv4 or FQDN), HOSTNAME and APP-NAME, opens the UDP socket, resolves the target. Returns false for an invalid target. Does not enable.|
|`void end()`|Closes the socket; configuration is kept, so `begin()` can follow.|
|`void setEnabled(bool)` / `bool isEnabled()`|Gate for every `send()`. Wire it to connected/disconnected events.|
|`void setMinSeverity(uint8_t)` / `uint8_t minSeverity()`|Messages numerically above it (less severe) are dropped. Default `SEV_DEBUG` (everything).|
|`void setFacility(uint8_t)`|Facility for every message. Default `FAC_LOCAL0`.|
|`void setProcId(const char*)`|PROCID field, e.g. a firmware version. Default NILVALUE.|
|`void setClock(ClockFn)`|`bool fn(int64_t& epochSec, uint32_t& usec)`: return false while the clock is not to be trusted. Default: no timestamp.|
|`bool resolveTarget()`|Resolves an FQDN target again (no-op for an IP). Call on network reconnect.|

### Sending

|Method|Meaning|
|---|---|
|`bool send(sev, msgid, const char* msg)`|One message; `msgid` is a short tag (MSGID), may be null. True if handed to the network stack.|
|`bool sendf(sev, msgid, fmt, ...)` / `vsendf(...)`|printf-style, formatted straight into the frame.|
|`bool send(sev, msgid, const __FlashStringHelper*)`|Flash-resident text: `F("...")`.|
|`bool sendf_P(sev, msgid, PGM_P fmt, ...)` / `vsendf_P(...)`|Flash-resident format: `PSTR("...")`.|

### Diagnostics

|Method|Meaning|
|---|---|
|`uint32_t sent()`|Datagrams handed to the stack since `begin()`.|
|`uint32_t dropped()`|Messages not sent since `begin()`: disabled, unresolved, below the minimum severity, did not fit, or the mutex was busy.|
|`const char* target()` / `const IPAddress& targetIp()`|The configured target and what it resolved to.|

### ESP32 hook (`SyslogEspLogHook.h`)

|Function|Meaning|
|---|---|
|`install(SyslogSender&, minSeverity = SEV_INFO)`|Routes every ESP-IDF log line to the sender; lines below `minSeverity` stay console-only. Requires `-DUSE_ESP_IDF_LOG`.|
|`uninstall()`|Restores the previous log output.|

### Pure helpers (`SyslogFormat.h`, `SyslogEspLogParse.h`)

Usable without a `SyslogSender`, for example in a host test or another
transport: `SyslogFormat::build()` / `buildHeader()` frame a message,
`SyslogFormat::timestamp()` formats an RFC 3339 instant,
`SyslogFormat::severityFromEspLevel()` maps an ESP-IDF level letter,
`SyslogEspLogParse::parse()` splits a log line into level, tag and message.

## Architecture

```txt
SyslogSender          Arduino side: UDP, mutex, target resolution, clock
├── SyslogFormat      pure: RFC 5424 framing, timestamp, severity mapping   (host-tested)
├── SyslogEspLogParse pure: ESP-IDF / Arduino log line -> level, tag, msg    (host-tested)
└── SyslogEspLogHook  ESP32 only: esp_log_set_vprintf() adapter
```

The two pure headers have no Arduino dependency and are exercised by the host
tests in `test/` (`./run_tests.sh`, `run_tests.bat`, or the VSCode task).

## Build options

|Macro|Default|Meaning|
|---|---|---|
|`SYSLOG_SENDER_MAX_LEN`|512 bytes (AVR: 160)|Largest frame sent; also the stack cost of a `send()`|
|`SYSLOG_SENDER_MUTEX_MS`|20 ms|How long a sender waits for the UDP mutex before dropping (ESP32)|

## Receiving

Any syslog server on UDP 514. rsyslog:

```txt
module(load="imudp")
input(type="imudp" port="514")
```

## License

MIT.
