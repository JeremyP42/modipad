/*
 * macro_player.h - Background macro executor.
 *
 * A macro is an ordered list of steps (key / text / multimedia / delay) that is
 * executed by a dedicated FreeRTOS task so the LVGL/touch loop is never
 * blocked. Steps are supplied as a JSON array (see config.json):
 *
 *   [
 *     {"type":"key","value":"CTRL+C","delay_ms":0},
 *     {"type":"text","value":"Hello","delay_ms":100},
 *     {"type":"delay","value":500},
 *     {"type":"multimedia","value":"PLAY_PAUSE","delay_ms":0}
 *   ]
 */
#ifndef MODIPAD_MACRO_PLAYER_H
#define MODIPAD_MACRO_PLAYER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the queue and start the worker task (idempotent). */
void macro_player_init(void);

/*
 * Enqueue a macro (JSON array as text). Returns false when the queue is full
 * or the input is invalid, so the caller can abort and show a toast.
 */
bool macro_play_json(const char *steps_json);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_MACRO_PLAYER_H */
