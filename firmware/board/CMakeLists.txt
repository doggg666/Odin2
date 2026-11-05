idf_component_register(
    SRCS "odin_board.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lvgl_port
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_i2c
                  esp_hw_support esp_lcd esp_lcd_ek79007 esp_lcd_touch_gt911
)
