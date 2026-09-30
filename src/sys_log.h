/*
 * sys_log.h - In-RAM ring buffer of recent log lines, served to the web UI.
 */
#ifndef MODIPAD_SYS_LOG_H
#define MODIPAD_SYS_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_LOG_LINES     64
#define SYS_LOG_LINE_LEN  128
#define SYS_LOG_BYTES     (SYS_LOG_LINES * SYS_LOG_LINE_LEN)

/* Install the log tee. Safe to call once, early in boot. */
void sys_log_init(void);

/* Copy the buffered log (oldest -> newest, newline separated) into `out`.
 * Returns the number of bytes written (without the terminating NUL). */
size_t sys_log_get(char *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_SYS_LOG_H */
