# Bundled build dependencies

Copied without source changes from the local Espressif Component Manager cache
so the firmware builds without registry access. Each component retains its
upstream license and manifest. `main/idf_component.yml` pins the versions and
points to these directories using relative `override_path` entries.

| Component | Version |
| --- | --- |
| lvgl/lvgl | 8.4.0 |
| espressif/esp_lvgl_port | 2.8.0~1 |
| espressif/esp_lcd_ek79007 | 1.0.4 |
| espressif/esp_lcd_touch_gt911 | 1.2.0~2 |
| espressif/esp_lcd_touch | 1.2.1 |
| espressif/cmake_utilities | 0.5.3 |

The LVGL port currently emits an upstream deprecation warning for
`esp_lcd_touch_get_coordinates`; this API remains available in the pinned
touch component. Do not upgrade LVGL to v9 without porting the Odin UI.

## V1.0 Wi-Fi dependencies

The following unmodified components were copied from Elecrow's official
`example/V1.0/idf-code/Lesson17-Wi-Fi_function/ESP32_P4-station/managed_components`
at repository commit `32fe6132f95835d4872d7aa673c685008ec4c37a`:

- espressif/esp_hosted 2.7.4
- espressif/esp_wifi_remote 0.16.3
- espressif/wifi_remote_over_eppp 0.3.2
- espressif/eppp_link 1.1.5
- espressif/esp_serial_slave_link (version retained in its upstream manifest)

The transitive EPPP components are bundled for dependency resolution; the
selected transport is ESP-Hosted SDIO. All retain their upstream licenses.
