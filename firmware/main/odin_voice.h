#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define ODIN_VOICE_URL "http://192.168.1.238:8001"
esp_err_t odin_voice_transcribe(const int16_t *pcm, size_t bytes, char *text, size_t size);
esp_err_t odin_voice_speak(const char *text, char *error, size_t size);
