#include "odin_model_catalog.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int odin_model_catalog_find(const odin_model_catalog_t *catalog, const char *id)
{
    for (unsigned i = 0; i < catalog->count; ++i)
        if (!strcmp(catalog->ids[i], id)) return (int)i;
    return -1;
}
static bool chat_id(const char *id)
{
    size_t length = strlen(id);
    if (!length || length >= ODIN_MODEL_ID_SIZE) return false;
    char lower[ODIN_MODEL_ID_SIZE];
    for (size_t i = 0; i <= length; ++i) {
        if (id[i] && (unsigned char)id[i] < 32) return false;
        lower[i] = (char)tolower((unsigned char)id[i]);
    }
    return !strstr(lower, "embedding");
}
bool odin_model_catalog_parse(const cJSON *json, odin_model_catalog_t *catalog,
                              char *error, size_t error_size)
{
    memset(catalog, 0, sizeof(*catalog));
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    if (!cJSON_IsArray(data)) {
        snprintf(error, error_size, "LM Studio returned an invalid model list.");
        return false;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, data) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (!cJSON_IsString(id) || !chat_id(id->valuestring)) continue;
        if (odin_model_catalog_find(catalog, id->valuestring) >= 0) continue;
        if (catalog->count == ODIN_MODEL_MAX) break;
        snprintf(catalog->ids[catalog->count++], ODIN_MODEL_ID_SIZE, "%s", id->valuestring);
    }
    if (!catalog->count) {
        snprintf(error, error_size, "No chat models available. Load a model in LM Studio and refresh.");
        return false;
    }
    return true;
}
