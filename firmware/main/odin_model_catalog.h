#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#define ODIN_MODEL_MAX 16
#define ODIN_MODEL_ID_SIZE 192
typedef struct {
    unsigned count;
    char ids[ODIN_MODEL_MAX][ODIN_MODEL_ID_SIZE];
} odin_model_catalog_t;
bool odin_model_catalog_parse(const cJSON *json, odin_model_catalog_t *catalog,
                              char *error, size_t error_size);
int odin_model_catalog_find(const odin_model_catalog_t *catalog, const char *id);
