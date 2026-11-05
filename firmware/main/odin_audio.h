#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define ODIN_AUDIO_RATE 16000
#define ODIN_RECORD_SECONDS 12
esp_err_t odin_audio_init(void);
/* Call settings_init after NVS initialization, before creating the UI. */
esp_err_t odin_audio_settings_init(void);
unsigned odin_audio_get_volume(void);
void odin_audio_set_volume(unsigned percent);
void odin_audio_save_volume(void);
/* Worker-only calls. Caller frees the PSRAM recording. */
esp_err_t odin_audio_record(atomic_bool *held, int16_t **pcm, size_t *bytes);
esp_err_t odin_audio_play(const int16_t *pcm, size_t bytes);
