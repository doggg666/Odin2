# Odin Wi-Fi station manager

Integrated with the V1.0 ESP32-C6 ESP-Hosted/esp_wifi_remote transport from
Elecrow's Lesson17 station example. Transport settings live in sdkconfig.defaults;
the dependencies are vendored under third_party. NVS is initialized by the Wi-Fi
worker before calling odin_wifi_init(). ESP-Hosted initializes its host transport
at startup, and esp_wifi_init() establishes the C6 connection.

All manager commands run serially in the main/odin_wifi_ui.c worker. Scan,
connect, and disconnect block and must never run from LVGL callbacks. Connect
waits for an IP address or a bounded retry/timeout; remembered credentials are
written only after success. Passwords are never logged. Every successful connection saves automatically in ordinary unencrypted NVS. Startup reconnects if saved credentials exist. Forget network deletes credentials
and marks default provisioning disabled, then clears the active radio config.
The private ignored wifi_defaults.local.h supplies one-time initial provisioning.

The UI polls worker results using an LVGL timer. Event group bits supply
connection status; only the worker starts/stops/configures the radio. Wi-Fi
events handle bounded reconnection and IP acquisition/loss.
