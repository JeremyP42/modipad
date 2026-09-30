/*
 * i18n.h - UI language selection and string translation.
 *
 * The selected language is persisted in config.json (settings.language) and
 * re-applied on boot. Changing the language rebuilds the LVGL UI immediately.
 */
#ifndef MODIPAD_I18N_H
#define MODIPAD_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LANG_EN = 0,
    LANG_RU = 1
} language_t;

/* Load the saved language (call after ui_loader_init()). */
void i18n_init(void);

/* Change the language: persists it and rebuilds the UI (deferred/safe). */
void i18n_set_language(language_t lang);

/* Current language. */
language_t i18n_get_language(void);

/* Translate a key ("wifi", "general", ...). Returns the key if unknown. */
const char *tr(const char *key);

/* Translate a config page display name ("Main", "Browser", ...). */
const char *tr_page(const char *name);

/* Rebuild the UI in the current language (runs inside the LVGL task). */
void i18n_refresh_ui(void);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_I18N_H */
