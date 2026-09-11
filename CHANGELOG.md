# Changelog

## [Unreleased]

### Fixed

- Halved the stack a hooked log line costs on the logging task: the header
  is sanitised straight into the frame instead of through RFC-maximum
  temporaries, and the ESP_LOG hook sends by length out of its line buffer
  instead of copying it. A 4 kB task overflowed with 0.1.1; the README now
  states the budget.

### Added

- `send(severity, msgid, msg, len)` for text that sits inside a larger
  buffer and is not NUL-terminated.

## [0.1.1] - 2026-09-11

### Fixed

- `begin()` no longer opens the UDP socket or resolves an FQDN: both need the
  network stack, and `begin()` is typically called before the interface is
  up (on ESP32 this asserted inside lwIP). The socket opens on the first
  `setEnabled(true)`; `resolveTarget()` is for the network-up event.

## [0.1.0] - 2026-09-11

### Added

- Initial release: RFC 5424 over UDP for ESP32, ESP8266 and AVR; ESP32
  `ESP_LOG` hook (requires `USE_ESP_IDF_LOG`); host tests for framing,
  timestamps and line parsing.

[Unreleased]: https://github.com/soosp/SyslogSender/compare/0.1.0...HEAD
[0.1.1]: https://github.com/soosp/SyslogSender/compare/0.1.0...0.1.1
[0.1.0]: https://github.com/soosp/SyslogSender/releases/tag/0.1.0
