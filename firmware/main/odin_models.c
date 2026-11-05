#include "odin_models.h"
#include "odin_ai.h"
#include "odin_wifi.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { bool select; char id[ODIN_MODEL_ID_SIZE]; } command_t;
static QueueHandle_t commands;
static SemaphoreHandle_t lock;
static odin_models_snapshot_t state;
static bool has_catalog;

static void status(const char *message, bool busy)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(state.status, sizeof(state.status), "%s", message);
    state.busy = busy; ++state.revision;
    xSemaphoreGive(lock);
}
static esp_err_t save_model(const char *id)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("odin_ai", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "model", id);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
static void refresh(void)
{
    if (!odin_wifi_is_connected()) { status("Connect to Wi-Fi before refreshing models.", false); return; }
    status("Refreshing models...", true);
    char error[128] = {0};
    cJSON *json = odin_ai_fetch_models(error, sizeof(error));
    if (!json) { status(error, false); return; }
    odin_model_catalog_t *catalog = calloc(1, sizeof(*catalog));
    if (!catalog) { cJSON_Delete(json); status("Not enough memory to list models.", false); return; }
    bool valid = odin_model_catalog_parse(json, catalog, error, sizeof(error));
    cJSON_Delete(json);
    xSemaphoreTake(lock, portMAX_DELAY);
    bool save_default = valid && !state.selected[0];
    xSemaphoreGive(lock);
    esp_err_t saved = save_default ? save_model(catalog->ids[0]) : ESP_OK;
    xSemaphoreTake(lock, portMAX_DELAY);
    state.catalog = *catalog;
    has_catalog = true;
    if (valid && !state.selected[0])
        snprintf(state.selected, sizeof(state.selected), "%s", catalog->ids[0]);
    bool selected_exists = valid && odin_model_catalog_find(catalog, state.selected) >= 0;
    if (selected_exists) snprintf(state.status, sizeof(state.status), "%u chat models available. Selection is remembered.", catalog->count);
    else snprintf(state.status, sizeof(state.status), "%s", valid ? "Saved model is unavailable. Choose another model." : error);
    if (saved != ESP_OK) snprintf(state.status, sizeof(state.status), "Model loaded, but saving the selection failed.");
    state.busy = false; ++state.revision;
    xSemaphoreGive(lock);
    if (save_default && saved == ESP_OK) ESP_LOGI("odin_models", "Default model selected and saved: %s", catalog->ids[0]);
    ESP_LOGI("odin_models", "Model list refreshed: %u chat models", catalog->count);
    free(catalog);
}
static void select_model(const char *id)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool available = odin_model_catalog_find(&state.catalog, id) >= 0;
    xSemaphoreGive(lock);
    if (!available) { status("Model is no longer listed. Refresh and choose again.", false); return; }
    esp_err_t err = save_model(id);
    if (err != ESP_OK) { status("Unable to save the selected model. Try again.", false); return; }
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(state.selected, sizeof(state.selected), "%s", id);
    snprintf(state.status, sizeof(state.status), "Model selected and saved. Used for your next message.");
    state.busy = false; ++state.revision;
    xSemaphoreGive(lock);
    ESP_LOGI("odin_models", "Selected model saved: %s", id);
}
static void worker(void *arg)
{
    command_t cmd;
    bool attempted = false;
    for (;;) {
        if (xQueueReceive(commands, &cmd, pdMS_TO_TICKS(500))) {
            if (cmd.select) select_model(cmd.id);
            else { refresh(); attempted = odin_wifi_is_connected(); }
        } else if (!attempted && odin_wifi_is_connected()) {
            refresh(); attempted = true;
        }
    }
}
esp_err_t odin_models_init(void)
{
    if (commands) return ESP_OK;
    lock = xSemaphoreCreateMutex();
    commands = xQueueCreate(4, sizeof(command_t));
    if (!lock || !commands) return ESP_ERR_NO_MEM;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("odin_ai", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t size = sizeof(state.selected);
        err = nvs_get_str(handle, "model", state.selected, &size);
        nvs_close(handle);
        if (err != ESP_OK) state.selected[0] = 0;
    }
    if (state.selected[0]) ESP_LOGI("odin_models", "Saved model loaded: %s", state.selected);
    snprintf(state.status, sizeof(state.status), "Waiting for Wi-Fi to load models...");
    state.revision = 1;
    if (xTaskCreate(worker, "odin_models", 8192, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
bool odin_models_refresh(void)
{
    command_t cmd = {0};
    return commands && xQueueSend(commands, &cmd, 0) == pdTRUE;
}
bool odin_models_select(const char *id)
{
    if (!commands || !id || !id[0] || strlen(id) >= ODIN_MODEL_ID_SIZE) return false;
    command_t cmd = {.select = true};
    snprintf(cmd.id, sizeof(cmd.id), "%s", id);
    return xQueueSend(commands, &cmd, 0) == pdTRUE;
}
bool odin_models_snapshot(odin_models_snapshot_t *snapshot, uint32_t *revision)
{
    if (!lock || !snapshot || !revision) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool changed = state.revision != *revision;
    if (changed) { *snapshot = state; *revision = state.revision; }
    xSemaphoreGive(lock);
    return changed;
}
bool odin_models_resolve(char *id, size_t size, char *error, size_t error_size)
{
    if (!lock) { snprintf(error, error_size, "Model service is not ready."); return false; }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool available = state.selected[0] && (!has_catalog || odin_model_catalog_find(&state.catalog, state.selected) >= 0);
    if (available && strlen(state.selected) < size) snprintf(id, size, "%s", state.selected);
    else {
        available = false;
        snprintf(error, error_size, "%s", state.selected[0] ? "Selected model is unavailable. Choose a model in Setting." : "Models are loading. Open Setting to refresh or choose a model.");
    }
    xSemaphoreGive(lock);
    return available;
}
