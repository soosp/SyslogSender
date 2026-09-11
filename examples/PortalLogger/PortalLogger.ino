// PortalLogger — feed a library's logger callback into syslog.
//
// AsyncConfigPortal (and other soosp libraries) do not own a transport: they
// expose setLogger(fn) and call fn(level, tag, msg). This adapts that to a
// SyslogSender. Sketch fragment; network setup and the portal's own begin()
// are as in the AsyncConfigPortal examples.

#include <Arduino.h>
#include <AsyncConfigPortal.h>
#include <SyslogSender.h>

SyslogSender      syslog;
AsyncConfigPortal portal(80);

static uint8_t severityOf(AsyncConfigPortal::LogLevel level) {
    switch (level) {
        case AsyncConfigPortal::LogLevel::Error:   return SyslogFormat::SEV_ERROR;
        case AsyncConfigPortal::LogLevel::Warn: return SyslogFormat::SEV_WARNING;
        case AsyncConfigPortal::LogLevel::Info:    return SyslogFormat::SEV_INFO;
        default:                                   return SyslogFormat::SEV_DEBUG;
    }
}

void setup() {
    Serial.begin(115200);
    // ... bring the network up ...

    syslog.begin("syslog.example.lan", "portal-demo", "portal");
    syslog.setEnabled(true);

    portal.setLogger([](AsyncConfigPortal::LogLevel level, const char* tag, const char* msg) {
        Serial.printf("[%s] %s\n", tag, msg);
        syslog.send(severityOf(level), tag, msg);
    });
    // ... portal.begin(...) ...
}

void loop() {}
