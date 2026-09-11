// Basic — send a few messages to a syslog server over WiFi (ESP32/ESP8266).
//
// On the server: rsyslog with `module(load="imudp")` and `input(type="imudp"
// port="514")`, or `docker run -p 514:514/udp mlesniew/syslog` for a quick
// look.

#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP32)
#  include <WiFi.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#  include <ESP8266WiFi.h>
#endif
#include <SyslogSender.h>

static const char* WIFI_SSID = "your-ssid";
static const char* WIFI_PASS = "your-password";
static const char* SYSLOG    = "192.168.1.10";   // IP or FQDN

SyslogSender syslog;

// Timestamp source: only once the clock has been set (here: by SNTP).
static bool clockNow(int64_t& sec, uint32_t& usec) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < 1700000000) return false;   // not set yet
    sec = tv.tv_sec; usec = tv.tv_usec;
    return true;
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) delay(200);
    configTime(0, 0, "pool.ntp.org");

    if (!syslog.begin(SYSLOG, "esp-example", "basic")) {
        Serial.println("syslog target is not a valid IP or FQDN");
        return;
    }
    syslog.setClock(clockNow);
    syslog.setFacility(SyslogFormat::FAC_LOCAL0);
    syslog.setEnabled(true);

    syslog.send(SyslogFormat::SEV_NOTICE, "boot", F("up"));            // flash-resident text
    syslog.sendf_P(SyslogFormat::SEV_INFO, "boot", PSTR("free heap %lu"),
                   (unsigned long)ESP.getFreeHeap());
}

void loop() {
    static uint32_t n = 0;
    syslog.sendf(SyslogFormat::SEV_INFO, "loop", "uptime %lu s, sent %lu, dropped %lu",
                 millis() / 1000, syslog.sent(), syslog.dropped());
    ++n;
    delay(5000);
}
