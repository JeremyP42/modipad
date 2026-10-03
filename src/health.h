/*
 * health.h - Startup diagnostics ("problems" report).
 *
 * A tiny fixed-size report filled once at boot. The status bar shows a red
 * warning icon when non-empty; Settings > Problems lists the entries.
 */
#ifndef MODIPAD_HEALTH_H
#define MODIPAD_HEALTH_H

#ifdef __cplusplus
extern "C" {
#endif

#define HEALTH_MAX_ITEMS 16
#define HEALTH_ITEM_LEN  112

/* Run all startup checks (SD card, config file, missing assets). */
void health_check_all(void);

/* Add one problem (called by other modules, e.g. the config loader). */
void health_add_problem(const char *text);

int health_problem_count(void);

/* Returns NULL for an out-of-range index. */
const char *health_problem(int index);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_HEALTH_H */
