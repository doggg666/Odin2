# Elecrow CrowPanel Advanced 10.1 V1.0 display integration

Hardware settings in `odin_board.c` are adapted exclusively from the official
Elecrow repository's `example/V1.0/idf-code/` sources:

- [Lesson07-Turn_on_the_screen](https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/tree/master/example/V1.0/idf-code/Lesson07-Turn_on_the_screen)
  — display, backlight, LDOs, PSRAM configuration. Inspected Git tree:
  `ae09b115a43fd08404a3260617b705ff37bd9a23`.
- [Lesson09-LVGL_Lighting_Control](https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/tree/master/example/V1.0/idf-code/Lesson09-LVGL_Lighting_Control)
  — I2C, GT911 touch, LVGL input registration. Inspected Git tree:
  `7f661480e819ff098865a9a524d45b0118bdf392`.

| Resource | V1.0 setting |
| --- | --- |
| LCD power | LDO3 at 2500 mV, LDO4 at 3300 mV |
| LCD | EK79007, 1024 × 600, RGB565 |
| DSI | Bus 0, 2 lanes, 900 Mbps |
| DPI pixel clock | 51 MHz |
| Horizontal back/pulse/front porch | 160 / 70 / 160 |
| Vertical back/pulse/front porch | 23 / 10 / 12 |
| Backlight | GPIO31, LEDC channel/timer 0, 30 kHz, 11 bits |
| Touch | GT911, reset GPIO40, interrupt GPIO42 |
| Touch I2C | Port 0, SDA45, SCL46, 400 kHz |

The integration preserves the vendor's power sequence and panel timings.
It corrects LVGL buffer sizing to pixels, stops on initialization errors, releases
an unsuccessful touch IO before trying the backup address, and creates the UI
under the LVGL lock. The board component owns these resources for the lifetime
of the firmware and rejects repeated initialization. Startup failure aborts;
it does not attempt to retry partial hardware initialization.

No V1.1/V1.2 pin maps are used. Camera, audio, and C6 transport remain separate
future integration work. Successful compilation does not validate the panel's
physical hardware.
