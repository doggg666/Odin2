/* Elecrow V1.0 Lesson11: PDM mic and direct I2S speaker path. */
#include "odin_audio.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include "nvs.h"

#define AMP_GPIO 30
static i2s_chan_handle_t rx, tx;
static bool ready;
static atomic_uint volume_percent = 50;
static TaskHandle_t settings_task;

unsigned odin_audio_get_volume(void) { return atomic_load(&volume_percent); }
void odin_audio_set_volume(unsigned percent)
{
    atomic_store(&volume_percent, percent > 100 ? 100 : percent);
}
void odin_audio_save_volume(void)
{
    if (settings_task) xTaskNotifyGive(settings_task);
}
static void save_settings(void *arg)
{
    unsigned saved = odin_audio_get_volume();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        unsigned value = odin_audio_get_volume();
        if (value == saved) continue;
        nvs_handle_t handle;
        esp_err_t err = nvs_open("odin_audio", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, "volume", value);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        if (err == ESP_OK) {
            saved = value;
            ESP_LOGI("odin_audio", "Speaker volume saved: %u%%", value);
        } else ESP_LOGW("odin_audio", "Unable to save volume: %s", esp_err_to_name(err));
    }
}
esp_err_t odin_audio_settings_init(void)
{
    if (settings_task) return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("odin_audio", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint8_t value;
        err = nvs_get_u8(handle, "volume", &value);
        nvs_close(handle);
        if (err == ESP_OK && value <= 100) odin_audio_set_volume(value);
        else if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) return err;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (xTaskCreate(save_settings, "audio_settings", 3072, NULL, 2, &settings_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI("odin_audio", "Speaker volume loaded: %u%%", odin_audio_get_volume());
    return ESP_OK;
}


esp_err_t odin_audio_init(void)
{
    if (ready) return ESP_OK;
    gpio_config_t amp = {.pin_bit_mask = 1ULL << AMP_GPIO, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&amp);
    if (err != ESP_OK) return err;
    gpio_set_level(AMP_GPIO, 1); /* Active low: mute until actual playback. */
    i2s_chan_config_t tx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    tx_cfg.dma_desc_num = 6; tx_cfg.dma_frame_num = 256; tx_cfg.auto_clear = true;
    err = i2s_new_channel(&tx_cfg, &tx, NULL);
    if (err != ESP_OK) goto fail;
    i2s_std_config_t output = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ODIN_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = 22, .ws = 21, .dout = 23, .din = I2S_GPIO_UNUSED},
    };
    err = i2s_channel_init_std_mode(tx, &output);
    if (err != ESP_OK) goto fail;
    i2s_chan_config_t rx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    rx_cfg.dma_desc_num = 6; rx_cfg.dma_frame_num = 256;
    err = i2s_new_channel(&rx_cfg, NULL, &rx);
    if (err != ESP_OK) goto fail;
    i2s_pdm_rx_config_t input = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(ODIN_AUDIO_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.clk = 24, .din = 26},
    };
    input.clk_cfg.dn_sample_mode = I2S_PDM_DSR_8S;
    input.clk_cfg.bclk_div = 8;
    err = i2s_channel_init_pdm_rx_mode(rx, &input);
    if (err != ESP_OK) goto fail;
    ready = true;
    ESP_LOGI("odin_audio", "V1.0 PDM microphone and I2S speaker initialized at 16000 Hz");
    return ESP_OK;
fail:
    if (rx) { i2s_del_channel(rx); rx = NULL; }
    if (tx) { i2s_del_channel(tx); tx = NULL; }
    return err;
}

esp_err_t odin_audio_record(atomic_bool *held, int16_t **pcm, size_t *bytes)
{
    *pcm = NULL; *bytes = 0;
    esp_err_t err = odin_audio_init();
    if (err != ESP_OK) return err;
    const size_t capacity = ODIN_AUDIO_RATE * 2 * ODIN_RECORD_SECONDS;
    int16_t *data = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) return ESP_ERR_NO_MEM;
    err = i2s_channel_enable(rx);
    if (err != ESP_OK) { free(data); return err; }
    size_t total = 0;
    while (atomic_load(held) && total < capacity) {
        size_t got = 0;
        size_t chunk = capacity - total > 1024 ? 1024 : capacity - total;
        err = i2s_channel_read(rx, (char *)data + total, chunk, &got, 200);
        if (err != ESP_OK || !got) { if (err == ESP_OK) err = ESP_ERR_TIMEOUT; break; }
        total += got;
    }
    i2s_channel_disable(rx);
    atomic_store(held, false);
    if (err != ESP_OK || total < ODIN_AUDIO_RATE / 2) {
        free(data); return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }
    /* Modest gain with saturation, rather than the vendor example's 10x gain. */
    uint64_t energy = 0;
    for (size_t i = 0; i < total / 2; ++i) {
        int32_t sample = data[i] * 2;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        data[i] = sample;
        energy += (int64_t)sample * sample;
    }
    ESP_LOGI("odin_audio", "Captured %u bytes; mean square level %llu", (unsigned)total,
             (unsigned long long)(energy / (total / 2)));
    *pcm = data; *bytes = total;
    return ESP_OK;
}

esp_err_t odin_audio_play(const int16_t *pcm, size_t bytes)
{
    if (!pcm || !bytes || bytes % 2) return ESP_ERR_INVALID_ARG;
    esp_err_t err = odin_audio_init();
    if (err != ESP_OK) return err;
    err = i2s_channel_enable(tx);
    if (err != ESP_OK) return err;
    unsigned previous_volume = odin_audio_get_volume();
    gpio_set_level(AMP_GPIO, previous_volume == 0);
    int16_t stereo[512];
    for (size_t offset = 0; offset < bytes / 2 && err == ESP_OK;) {
        size_t count = bytes / 2 - offset;
        if (count > 256) count = 256;
        unsigned volume = odin_audio_get_volume();
        if (volume != previous_volume) {
            gpio_set_level(AMP_GPIO, volume == 0);
            previous_volume = volume;
        }
        for (size_t i = 0; i < count; i++) {
            /* Bound gain to unity and apply equally to both hardware slots. */
            stereo[i * 2] = stereo[i * 2 + 1] = (int32_t)pcm[offset + i] * (int32_t)volume / 100;
        }
        size_t sent = 0;
        err = i2s_channel_write(tx, stereo, count * 4, &sent, 1000);
        if (err == ESP_OK && sent != count * 4) err = ESP_ERR_TIMEOUT;
        offset += count;
    }
    /* Flush the DMA queue before muting the last syllable. */
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(120));
    gpio_set_level(AMP_GPIO, 1);
    i2s_channel_disable(tx);
    return err;
}
