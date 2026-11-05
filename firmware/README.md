# Odin firmware — CrowPanel Advanced 10.1 V1.0

The firmware initializes the V1.0 LCD power rails, EK79007 MIPI-DSI display,
PWM backlight, GT911 touch controller, and LVGL task, then creates the Odin UI.
The screen reports Ready and AI: OFFLINE. Wi-Fi settings provide network scanning,
password entry, connection status, disconnect, and automatic saved reconnect and Forget network.
LM Studio text chat is integrated. PDM microphone recording and I2S speaker playback are integrated. Camera is not connected.

## Build

The tested setup is ESP-IDF **5.5.0** with its matching RISC-V compiler.
The workspace VS Code setup points to that installation. From this directory:

```bash
./build.sh
```

The helper selects `/home/logitexx/.espressif/v5.5/esp-idf` by default, activates
its tools, and builds. Set `ODIN_IDF_PATH` to an ESP-IDF 5.5.0 installation in
another location if needed. Alternatively, in an already activated 5.5 terminal:

```bash
idf.py build
```

The required third-party components are bundled in `third_party/`, with exact
versions and relative overrides in `main/idf_component.yml`. An installed IDF
and compiler are still required, but component registry access is not.
LVGL is pinned to **8.4.0**, which supports the existing LVGL 8.x UI APIs.

Outputs are `build/odin_crowpanel_v1_0.bin`, `build/bootloader/bootloader.bin`,
and `build/partition_table/partition-table.bin`. The app uses `partitions.csv`, with a 3 MB factory app on 16 MB flash;
NVS and PHY offsets are unchanged. OTA is not configured.

## Flash and check the panel

Connect the panel's programming USB port, replace the serial port below with
the actual device, then run:

```bash
./build.sh -p /dev/ttyUSB0 flash monitor
```

Expected log: `Odin UI ready; display and touch initialized`. The display should
show the Odin orb, Ready, and the offline message. Touch is registered with LVGL;
the current UI has no network picker or action buttons. The build has been
verified locally; display output and touch still need checking on the hardware.

## Hardware and configuration

`board/odin_board.c` owns display power, backlight, DSI, touch I2C, and LVGL.
Do not initialize those resources elsewhere. It follows Elecrow's official
V1.0 examples; see `board/README.md` for the sources and exact settings.
All Odin UI creation and setters must run inside `lvgl_port_lock()` /
`lvgl_port_unlock()`, or from an LVGL callback. Startup creates the UI before
enabling the backlight.

`sdkconfig.defaults` enables 200 MHz PSRAM, RGB565, 16 MB flash, and the chip
revision range from Elecrow's V1.0 example (minimum v0.1, maximum v1.99).
Board version and chip revision are separate: if a panel reports a different
chip revision, confirm it before changing the target configuration.

The previous build directory and configuration files are preserved in
`backups/before-ui-integration/`. They mixed ESP-IDF 6.0.1 generated flags with
a 5.5 compiler. Use a fresh build directory whenever changing IDF versions.

## Wi-Fi settings

Open **Setting → Wi-Fi settings**, select a 2.4 GHz network (or type a hidden SSID), enter
its password with the floating keyboard, and tap **Connect**. Tap either field
to open the keyboard; drag its title bar to move it, or tap **Hide** to dismiss.
Done advances from SSID to password, then dismisses the keyboard. Passwords
stay masked. An empty password selects an open network.

Successful connections automatically save their SSID and password in host NVS
after obtaining an IP address. Startup reconnects to the saved network. The
**Forget network** button erases saved credentials and disconnects. Forgetting
also disables one-time default provisioning, so the default cannot silently
return on reboot. **Disconnect** leaves credentials saved for the next boot.
The UI currently stores one network; a new successful connection replaces it.
Credentials use ordinary, unencrypted NVS.

A private `components/odin_wifi/wifi_defaults.local.h` supplies the requested
initial SSID/password. It is ignored by git. The initial default is attempted
once until successful (or forgotten), then NVS becomes authoritative. Do not
publish that local header or compiled device images containing its credentials.
This header is optional. Without it, connect through the panel UI. Copy
`wifi_defaults.example.h` to `wifi_defaults.local.h` to provide initial credentials.

The home screen shows the assigned IP address; AI status reflects LM Studio.
Scans and connections run in a worker task, keeping rendering responsive.

The V1.0 transport follows Elecrow Lesson17's station example: ESP-Hosted 2.7.4,
esp_wifi_remote 0.16.3, C6 target, one-bit SDIO slot 1, CLK18/CMD19/D0=14/D1=15,
reset GPIO32 active high, 40 MHz SDIO and 1500 ms reset delay. The existing C6
firmware is used; this application does not reflash the coprocessor.

ESP-IDF 5.5 requires `CONFIG_IDF_EXPERIMENTAL_FEATURES=y` to enable the panel's
200 MHz PSRAM setting. Without it, Kconfig falls back to 20 MHz and the display
underruns. Both active configuration and defaults retain the required flag.

Before Wi-Fi integration, application/configuration files were copied to
`backups/before-wifi-integration/`.

Hardware verification: the flashed build initialized the existing C6 over the
V1.0 one-bit SDIO link, scanned 16 networks, and obtained a station IP address.
The C6 reports ESP-Hosted 2.12.0; the vendor host stack reports 2.7.0 on the wire
(manifest 2.7.4) and emits a compatibility warning. Scanning and DHCP work in
this test; prolonged connectivity and saved reconnect across power cycles have
not yet been validated. No C6 firmware was modified.

## LM Studio text chat

The base URL in `main/odin_ai.c` is `http://192.168.1.93:1234/v1`.
Tap **Talk to Odin**, enter a message, dismiss the keyboard with Done, and tap
**Send**. Replies appear in a scrollable text area. Network work runs on a
separate task. Model discovery uses `/models`. Select a chat model in **Setting → AI model**;
embedding models are excluded. The selected ID is saved to NVS, restored on
startup, and used by typed and voice requests. The first available chat model
is selected only when no saved choice exists. If a saved model is missing,
choose another explicitly. **Refresh** reloads the list without blocking touch.
Generation uses `/chat/completions` with non-streaming output, a 512-token
limit, a 90-second HTTP timeout, and a bounded 32 KB response buffer. Messages
are independent requests; multi-turn conversation history is not implemented.
The endpoint has no configured authorization header; the supplied LAN server
accepted model discovery and a test completion without authentication.

The desktop endpoint test returned `Odin connection works` from
`qwen3.8-35b-a3b-distill`. Board-side model discovery is logged separately as
`LM Studio model discovery succeeded`. A successful discovery confirms server
reachability, not completion generation. Wi-Fi must reach the server's LAN.
API shape follows https://lmstudio.ai/docs/developer/openai-compat.

On the first flashed AI build, display startup and scanning passed, but the
board rejoined its previous network at 10.0.0.29 and the LM Studio probe timed
out. Before this flash, joining the server's LAN had assigned 192.168.1.153.
The automatic-save update provisions the requested default LAN and persists it. Each Send retries
model discovery, so no reboot is needed after changing Wi-Fi. A board-side chat
completion remains to be checked after restoring server reachability.

Automatic-save hardware check: provisioned default connected at 192.168.1.153,
logged a successful NVS credential save, and reached LM Studio model discovery.
After a monitor-triggered hardware reset, it connected again at 192.168.1.153
from saved credentials without running the default provisioning branch, and
LM Studio discovery succeeded again. Forget is implemented in the Wi-Fi worker;
its touch interaction has not yet been exercised on the panel.

## Voice input and spoken replies

See `../ai-server/VOICE.md` for the running local speech bridge, startup command,
V1.0 audio settings, and Hold to talk / mic test / Speak replies controls.

## Interface styling

The charcoal-and-sage interface uses shared button, panel, input, and keyboard
styles in `odin_ui/odin_ui.c`. Home offers a conversation entry point and Wi-Fi
settings, with AI and network status at the edges. Chat groups typed input,
hold-to-talk, mic testing, and spoken-reply controls. Wi-Fi retains automatic
credential saving, Forget, and its movable keyboard. Larger Montserrat fonts
are enabled in both the active configuration and defaults.

`docs/ui/home.png` and `docs/ui/chat.png` are 1024x600 native LVGL host previews
with sample connected-state text; they are not photographs of the panel.
Pre-refresh source files are in `backups/before-ui-refresh/`.

Speaker volume is adjustable from 0–100% only in Setting, with 0% muting the
amplifier. Both TTS replies and the mic test use the same gain. The slider
updates playback per audio block. Releasing it saves the setting in the
`odin_audio` NVS namespace through a worker task; startup restores it before
creating the UI. The initial level is 50%. Forget network only clears Wi-Fi
credentials and does not change volume.

## Battery and Setting page

The home header shows a battery icon, reported percentage, and voltage. A cog
button opens **Setting**, with battery and charging details, saved speaker
volume, and a shortcut to Wi-Fi settings. The STC8 is read every five seconds
on the existing touch I2C bus (GPIO45/46), at address 0x2F, using the supplied
bsp_stc8h1kxx driver's byte-by-byte register protocol. The percentage is an
estimate from the controller, not a calibrated state-of-charge measurement.
If the controller is absent, a transaction fails, or data is invalid, the UI
shows unavailable instead of displaying a previous or fabricated reading.

The native LVGL preview `docs/ui/settings.png` shows the Setting page with
sample battery values. Startup used 48% of the configured LVGL heap during the
hardware check. The STC8 acknowledged and returned battery data. Per the requested power-source
rule, valid readings above 4.3 V display USB power; readings at or below 4.3 V
display battery voltage, percentage, and charging state. This is a UI heuristic,
not an independent USB-presence measurement. The cog is hidden on every page
except Home, and speaker volume is controlled only from Setting.


The Home Wi-Fi action and former two-line headline have been removed. Wi-Fi
configuration is available from Setting, beside volume and the AI model selector.
Model listing and selection persistence run in a worker separate from chat,
so refreshing Setting does not block touch or an in-progress conversation.
A model change applies to the next chat request, not an answer already generating.
The model list supports up to 16 unique IDs of at most 191 UTF-8 bytes; malformed
IDs and embedding IDs are excluded. The Settings selector was checked against
the live server (four chat models) and native LVGL UI selection tests.

Home uses the user-supplied Odin face artwork in place of the former waveform
and rings. The original image and its 340x340 RGB565 display asset are retained
in `odin_ui/assets/`; the display asset is embedded in flash with a native LVGL
image descriptor. It requires no runtime download, image decoder, or extra
full-image heap allocation. Asset conversion instructions are in that folder.
The 1024x600 Home preview in `docs/ui/home.png` reflects the new artwork.

## Montreal date and time

Home's lower-left footer shows Montreal, Quebec's date and time in 24-hour
format, including seconds and EST/EDT. It updates once per second. The former
footer sentence is removed. A worker starts ESP-IDF SNTP after Wi-Fi connects,
using `time.nrc.ca` with `pool.ntp.org` as a second server. The UI shows Waiting
for time sync before a valid clock is available, and the clock keeps ticking
through temporary Wi-Fi outages after synchronization. Cold boots need network
time again. The servers are queried periodically by SNTP (one-hour interval).

The timezone is `EST5EDT,M3.2.0/2,M11.1.0/2`, implementing Montreal's seasonal
Eastern time rules. Tests covered winter/summer offsets, both 2026 DST
transitions, and the pre-sync and short-buffer cases. Reference:
https://www.quebec.ca/gouvernement/portrait-quebec/changement-heure

## Screen power saver

Enable **Setting → Power saver** to turn off the backlight after ten minutes
without touch. Touch the screen to wake it at its previous brightness; the first
gesture only wakes the screen. Clock updates and network activity do not reset
the timeout. The switch defaults to off and is saved in NVS across restarts.
Wi-Fi, voice services, and the processor continue running while the screen sleeps.
