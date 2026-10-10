#if defined(ARDUINO_ARCH_ESP32)

#include "SyslogEspLogHook.h"
#include "SyslogEspLogParse.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if __has_include(<freertos/idf_additions.h>)
#include <freertos/idf_additions.h>   // pxTaskGetStackStart()
#endif
#include <atomic>

namespace {

SyslogSender*         s_sender = nullptr;
uint8_t               s_minSeverity = SyslogFormat::SEV_INFO;
vprintf_like_t        s_previous = nullptr;
std::atomic<bool>     s_inHook{false};
std::atomic<uint32_t> s_skipped{0};

// Free bytes on the calling task's stack below this point. Stacks grow down
// on both ESP32 cores (Xtensa and RISC-V). The position is a local's address:
// __builtin_frame_address(0) reads the frame-pointer register, which code
// built without frame pointers uses as an ordinary register, so it can be
// anything.
__attribute__((noinline)) size_t stackFree() {
    volatile uint8_t mark = 0;
    const uint8_t* bottom = pxTaskGetStackStart(nullptr);
    const uint8_t* here   = const_cast<const uint8_t*>(&mark);
    return (bottom && here > bottom) ? static_cast<size_t>(here - bottom) : 0;
}

// The forwarding half, with the line buffer. Kept out of hook() so that the
// console output, which runs first, does not run with this frame on the
// stack: in a task with a small stack (the ESP-IDF event task, 2.5 kB) the
// two together overflow it.
__attribute__((noinline)) void forward(const char* fmt, va_list ap) {
    char line[SYSLOG_SENDER_MAX_LEN];
    vsnprintf(line, sizeof(line), fmt, ap);

    const SyslogEspLogParse::Parsed p = SyslogEspLogParse::parse(line);
    // A line without a level letter is INFO; both arms as uint8_t, since
    // severityFromEspLevel() returns uint8_t and SEV_INFO is an enumerator.
    const uint8_t sev = p.level ? SyslogFormat::severityFromEspLevel(p.level)
                                : static_cast<uint8_t>(SyslogFormat::SEV_INFO);
    if (sev > s_minSeverity) return;
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

int hook(const char* fmt, va_list ap) {
    // Console first, always, with only this small frame on the stack.
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

    // Forwarding needs the line buffer, the frame and the UDP send on this
    // task's stack. A task without that much room left keeps the line on
    // the console only; skipped() counts them.
    if (stackFree() < SYSLOG_ESPLOG_STACK_RESERVE) {
        s_skipped.fetch_add(1, std::memory_order_relaxed);
        return written;
    }

    if (s_inHook.exchange(true)) return written;   // nested log from the stack
    forward(fmt, ap);
    s_inHook.store(false);
    return written;
}

} // namespace

void SyslogEspLogHook::install(SyslogSender& sender, uint8_t minSeverity) {
    s_sender      = &sender;
    s_minSeverity = minSeverity;
    if (!s_previous) s_previous = esp_log_set_vprintf(hook);
}

uint32_t SyslogEspLogHook::skipped() {
    return s_skipped.load(std::memory_order_relaxed);
}

void SyslogEspLogHook::uninstall() {
    if (s_previous) { esp_log_set_vprintf(s_previous); s_previous = nullptr; }
    s_sender = nullptr;
}

#endif // ARDUINO_ARCH_ESP32
