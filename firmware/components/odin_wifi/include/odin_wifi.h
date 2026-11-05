#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* V1.0 C6 transport is configured in sdkconfig; call from one worker task.
 * Connect and scan block. NVS must be initialized before init.
 * Credentials are persisted only after a successful connection automatically. */
esp_err_t odin_wifi_init(void);
esp_err_t odin_wifi_connect(const char *ssid, const char *password);
esp_err_t odin_wifi_disconnect(void);
esp_err_t odin_wifi_forget(void);
esp_err_t odin_wifi_scan(wifi_ap_record_t *results, uint16_t *count);
bool odin_wifi_is_connected(void);
esp_err_t odin_wifi_connect_saved(void);

#ifdef __cplusplus
}
#endif
