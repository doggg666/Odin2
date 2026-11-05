/* Hardware settings adapted from Elecrow's example/V1.0/idf-code/
 * Lesson07-Turn_on_the_screen and Lesson09-LVGL_Lighting_Control.
 * See board/README.md for provenance. Do not substitute other board revisions.
 */
#include "odin_board.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"

#define LCD_WIDTH 1024
#define LCD_HEIGHT 600
#define BACKLIGHT_GPIO 31
#define TOUCH_SDA_GPIO 45
#define TOUCH_SCL_GPIO 46
#define TOUCH_RESET_GPIO 40
#define TOUCH_INTERRUPT_GPIO 42

static const char *TAG = "odin_board";
static esp_ldo_channel_handle_t ldo3, ldo4;
static esp_lcd_dsi_bus_handle_t dsi_bus;
static esp_lcd_panel_io_handle_t panel_io, touch_io;
static esp_lcd_panel_handle_t panel;
static i2c_master_bus_handle_t i2c_bus;
static esp_lcd_touch_handle_t touch;
static bool backlight_ready;
static bool init_started;

esp_err_t odin_board_set_backlight(uint32_t brightness)
{
    if (brightness > 100) return ESP_ERR_INVALID_ARG;
    if (!backlight_ready) return ESP_ERR_INVALID_STATE;
    uint32_t duty = brightness ? brightness * 18 + 200 : 0;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty),
                        TAG, "set backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_11_BIT,
        .freq_hz = 30000,
        .clk_cfg = LEDC_USE_PLL_DIV_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    const ledc_channel_config_t channel = {
        .gpio_num = BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel");
    backlight_ready = true;
    return ESP_OK;
}

static esp_err_t touch_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA_GPIO,
        .scl_io_num = TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "touch I2C bus");
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 16,
        .flags.disable_control_phase = 1,
        .scl_speed_hz = 400000,
    };
    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_WIDTH,
        .y_max = LCD_HEIGHT,
        .rst_gpio_num = TOUCH_RESET_GPIO,
        .int_gpio_num = TOUCH_INTERRUPT_GPIO,
        .levels = {.reset = 0, .interrupt = 0},
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &touch_io),
                        TAG, "touch panel IO");
    esp_err_t err = esp_lcd_touch_new_i2c_gt911(touch_io, &touch_cfg, &touch);
    if (err == ESP_OK) return ESP_OK;

    /* Elecrow also tries the backup address. Release the first IO first. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(touch_io), TAG, "release touch IO");
    touch_io = NULL;
    io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &touch_io),
                        TAG, "backup touch panel IO");
    return esp_lcd_touch_new_i2c_gt911(touch_io, &touch_cfg, &touch);
}

static esp_err_t panel_init(void)
{
    const esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 900,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), TAG, "DSI bus");
    const esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &panel_io),
                        TAG, "DSI panel IO");
    const esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 51,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = LCD_WIDTH,
            .v_size = LCD_HEIGHT,
            .hsync_back_porch = 160,
            .hsync_pulse_width = 70,
            .hsync_front_porch = 160,
            .vsync_back_porch = 23,
            .vsync_pulse_width = 10,
            .vsync_front_porch = 12,
        },
        .flags.use_dma2d = true,
    };
    ek79007_vendor_config_t vendor_cfg = {
        .mipi_config = {.dsi_bus = dsi_bus, .dpi_config = &dpi_cfg},
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ek79007(panel_io, &panel_cfg, &panel),
                        TAG, "EK79007 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    return esp_lcd_panel_init(panel);
}

static esp_err_t display_lvgl_init(void)
{
    const lvgl_port_cfg_t port_cfg = {
        .task_priority = configMAX_PRIORITIES - 4,
        .task_stack = 16384,
        .task_affinity = -1,
        .task_max_sleep_ms = 10,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL port");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = panel,
        /* The port expects pixels, not bytes. Keep buffers in PSRAM. */
        .buffer_size = LCD_WIDTH * LCD_HEIGHT,
        .double_buffer = true,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .flags.buff_spiram = true,
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {0};
    lv_display_t *display = lvgl_port_add_disp_dsi(&display_cfg, &dsi_cfg);
    ESP_RETURN_ON_FALSE(display, ESP_FAIL, TAG, "register LVGL display");
    const lvgl_port_touch_cfg_t touch_cfg = {.disp = display, .handle = touch};
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG,
                        "register LVGL touch");
    return ESP_OK;
}

esp_err_t odin_board_init(void)
{
    /* Startup is fail-fast; never attempt to acquire the same resources twice. */
    if (init_started) return ESP_ERR_INVALID_STATE;
    init_started = true;
    const esp_ldo_channel_config_t ldo3_cfg = {.chan_id = 3, .voltage_mv = 2500};
    const esp_ldo_channel_config_t ldo4_cfg = {.chan_id = 4, .voltage_mv = 3300};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo3_cfg, &ldo3), TAG, "LDO3");
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo4_cfg, &ldo4), TAG, "LDO4");
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(touch_init(), TAG, "GT911 touch");
    ESP_RETURN_ON_ERROR(panel_init(), TAG, "display panel");
    return display_lvgl_init();
}

/* Matches the supplied bsp_stc8h1kxx driver: 0x2F, byte-wise register reads.
 * Decode explicitly instead of relying on compiler structure padding. */
static i2c_master_dev_handle_t battery_device;
static uint32_t battery_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
esp_err_t odin_board_read_battery(odin_battery_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    if (!i2c_bus) return ESP_ERR_INVALID_STATE;
    if (!battery_device) {
        esp_err_t err = i2c_master_probe(i2c_bus, 0x2F, 100);
        if (err != ESP_OK) return err;
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x2F,
            .scl_speed_hz = 400000,
        };
        err = i2c_master_bus_add_device(i2c_bus, &cfg, &battery_device);
        if (err != ESP_OK) return err;
    }
    uint8_t raw[12] = {0};
    for (uint8_t reg = 0; reg < sizeof(raw); ++reg) {
        esp_err_t err = i2c_master_transmit_receive(battery_device, &reg, 1, raw + reg, 1, 100);
        if (err != ESP_OK) return err;
    }
    odin_battery_info_t sample = {
        .adc_mv = battery_u32_le(raw),
        .battery_mv = battery_u32_le(raw + 4),
        .percent = raw[8], .charge_state = raw[9], .led_state = raw[10],
    };
    if (sample.adc_mv > 3600 || sample.battery_mv < 2000 || sample.battery_mv > 5000 ||
        sample.percent > 100 || sample.charge_state > 4 || sample.led_state > 4)
        return ESP_ERR_INVALID_RESPONSE;
    *info = sample;
    return ESP_OK;
}
