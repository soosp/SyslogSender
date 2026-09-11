#if defined(ARDUINO_ARCH_ESP32)

#include "SyslogEspLogHook.h"
#include "SyslogEspLogParse.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

namespace {

SyslogSender*       s_sender = nullptr;
uint8_t             s_minSeverity = SyslogFormat::SEV_INFO;
vprintf_like_t      s_previous = nullptr;
std::atomic<bool>   s_inHook{false};

int hook(const char* fmt, va_list ap) {
    // Serial first, always, exactly as before.
    int written = 0;
    if (s_previous) {
        va_list ap2;
        va_copy(ap2, ap);
        written = s_previous(fmt, ap2);
        va_end(ap2);
    }

    if (!s_sender || !s_sender->isEnabled()) return written;
    if (xPortInIsrContext()) return written;

    // ESP-IDF's format string starts with the level letter (after an optional
    // colour escape), so a line below the floor — the sender's or the hook's
    // — is dropped before anything is formatted. Debug lines cost nothing
    // when only INFO and above are forwarded.
    {
        const char* f = SyslogEspLogParse::skipAnsi(fmt);
        if (SyslogEspLogParse::isLevel(f[0])) {
            const uint8_t sev = SyslogFormat::severityFromEspLevel(f[0]);
            if (sev > s_minSeverity || sev > s_sender->minSeverity()) return written;
        }
    }

    if (s_inHook.exchange(true)) return written;   // nested log from the stack

    char line[SYSLOG_SENDER_MAX_LEN];
    vsnprintf(line, sizeof(line), fmt, ap);

    const SyslogEspLogParse::Parsed p = SyslogEspLogParse::parse(line);
    const uint8_t sev = p.level ? SyslogFormat::severityFromEspLevel(p.level)
                                : SyslogFormat::SEV_INFO;
    if (sev <= s_minSeverity) {
        char tag[SyslogFormat::MAX_MSGID + 1];
        const char* msgid = nullptr;
        if (p.tag) {
            size_t n = p.tagLen < SyslogFormat::MAX_MSGID ? p.tagLen : SyslogFormat::MAX_MSGID;
            memcpy(tag, p.tag, n);
            tag[n] = '\0';
            msgid = tag;
        }
        // Sent by length, straight out of the line buffer: no second copy on
        // this task's stack.
        s_sender->send(sev, msgid, p.msg, p.msgLen);
    }

    s_inHook.store(false);
    return written;
}

} // namespace

void SyslogEspLogHook::install(SyslogSender& sender, uint8_t minSeverity) {
    s_sender      = &sender;
    s_minSeverity = minSeverity;
    if (!s_previous) s_previous = esp_log_set_vprintf(hook);
}

void SyslogEspLogHook::uninstall() {
    if (s_previous) { esp_log_set_vprintf(s_previous); s_previous = nullptr; }
    s_sender = nullptr;
}

#endif // ARDUINO_ARCH_ESP32
