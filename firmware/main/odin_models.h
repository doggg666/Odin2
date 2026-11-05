#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "odin_model_catalog.h"
typedef struct {
    odin_model_catalog_t catalog;
    char selected[ODIN_MODEL_ID_SIZE];
    char status[128];
    bool busy;
    uint32_t revision;
} odin_models_snapshot_t;
esp_err_t odin_models_init(void);
bool odin_models_refresh(void);
bool odin_models_select(const char *id);
/* UI gets an updated snapshot only when revision changes. */
bool odin_models_snapshot(odin_models_snapshot_t *snapshot, uint32_t *revision);
bool odin_models_resolve(char *id, size_t size, char *error, size_t error_size);
