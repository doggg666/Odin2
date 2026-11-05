#pragma once
#include <stddef.h>
#include "cJSON.h"
/* Create under the LVGL lock. HTTP requests run in a worker task. */
void odin_ai_create(void);

/* Shared LM Studio GET helper; each request owns its HTTP client. */
cJSON *odin_ai_fetch_models(char *error, size_t error_size);
