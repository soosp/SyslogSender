// EspLogHook — forward every ESP_LOGx line to syslog (ESP32 only).
//
// Build with -DUSE_ESP_IDF_LOG (platformio.ini build_flags, or the Arduino
// IDE's "Core Debug Level" plus a #define before any include). Without it the
// Arduino core prints its "[ms][X][file:line]" lines straight to the console
// and esp_log_set_vprintf() never sees them.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <SyslogSender.h>
#include <SyslogEspLogHook.h>

SyslogSender syslog;

static bool clockNow(int64_t& sec, uint32_t& usec) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < 1700000000) return false;
    sec = tv.tv_sec; usec = tv.tv_usec;
    return true;
}

void setup() {
    Serial.begin(115200);
    esp_log_level_set("*", ESP_LOG_DEBUG);   // IDF defaults to INFO

    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) delay(200);
    configTime(0, 0, "pool.ntp.org");

    syslog.begin("192.168.1.10", "esp-hook", "myapp");
    syslog.setClock(clockNow);
    syslog.setEnabled(true);

    // From here on E, W and I lines go to syslog as well as the console;
    // D and V stay on the console.
    SyslogEspLogHook::install(syslog, SyslogFormat::SEV_INFO);

    ESP_LOGI("app", "hook installed");
}

void loop() {
    ESP_LOGW("app", "a warning, forwarded");
    ESP_LOGD("app", "a debug line, console only");
    delay(10000);
}
