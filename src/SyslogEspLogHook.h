#pragma once

/**
 * @file SyslogEspLogHook.h
 * @brief Forwards every ESP_LOGx line to a SyslogSender. ESP32 only.
 *
 * Installs a vprintf hook with esp_log_set_vprintf(). Each log line still
 * goes to the previous destination (the serial console) unchanged; a copy is
 * parsed (SyslogEspLogParse.h) and sent with the level mapped to a syslog
 * severity and the ESP-IDF tag as MSGID.
 *
 * **Build with -DUSE_ESP_IDF_LOG.** The Arduino core's default logging
 * (ESP_LOGx redefined to log_printf, the "[ms][X][file:line] func():" lines)
 * writes straight to the ROM console and never passes through
 * esp_log_set_vprintf(). With USE_ESP_IDF_LOG both the core and the
 * application log through ESP-IDF, in its "X (ms) tag: msg" format, and every
 * line reaches this hook. Set the runtime level as well —
 * esp_log_level_set("*", ESP_LOG_DEBUG) — since IDF defaults to INFO.
 *
 * Reentrancy: the network stack may itself log while sending; a per-hook
 * flag makes such nested lines serial-only, so a send can never recurse. A
 * line logged from an ISR is serial-only as well.
 *
 * Cost: the line is formatted twice (once for serial, once for the frame),
 * into two stack buffers in the logging task's context. Size them with
 * SYSLOG_SENDER_MAX_LEN; a task that logs with a small stack should be given
 * room for it.
 */

#if defined(ARDUINO_ARCH_ESP32)

#include "SyslogSender.h"

namespace SyslogEspLogHook {

/**
 * @brief Installs the hook. Call once, after Serial is set up.
 * @param sender      Where lines go. Must outlive the hook.
 * @param minSeverity Lowest severity forwarded; lines below it (numerically
 *                    above it) stay serial-only. SEV_INFO forwards E, W and I.
 *                    The sender's own setMinSeverity() applies as well, so a
 *                    hook installed with SEV_DEBUG lets a runtime setting on
 *                    the sender decide; either floor is checked before the
 *                    line is formatted, so a dropped line costs nothing.
 */
void install(SyslogSender& sender, uint8_t minSeverity = SyslogFormat::SEV_INFO);

/** @brief Restores the previous vprintf. */
void uninstall();

} // namespace SyslogEspLogHook

#endif // ARDUINO_ARCH_ESP32
