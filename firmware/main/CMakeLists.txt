idf_component_register(
    SRCS "main.c" "odin_wifi_ui.c" "odin_ai.c" "odin_audio.c" "odin_settings.c" "odin_models.c" "odin_clock.c" "odin_power.c" "odin_model_catalog.c" "odin_voice.c" "../odin_ui/odin_ui.c" "../odin_ui/odin_face.c" "../odin_ui/odin_time.c"
    INCLUDE_DIRS "." "../odin_ui"
    PRIV_REQUIRES board esp_hw_support odin_wifi nvs_flash esp_http_client esp_netif json esp_driver_i2s esp_driver_gpio
)

# Fixed-size artwork stored in flash; keep a stable symbol name for LVGL.
target_add_binary_data(${COMPONENT_LIB} "../odin_ui/assets/odin_face.rgb565" BINARY RENAME_TO odin_face)
