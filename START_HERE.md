# Odin source package

This archive contains the current CrowPanel Advanced 10.1 V1.0 firmware,
bundled component sources, UI previews, and local AI/voice-server code.

Firmware: see firmware/README.md. The tested environment is ESP-IDF 5.5.
The active sdkconfig is included to preserve the working display/audio setup.
Adjust ODIN_IDF_PATH when running firmware/build.sh on another computer.

Wi-Fi: the included wifi_defaults.local.h has empty initial credentials.
Use Wi-Fi settings on the panel to connect; successful connections save in NVS.
To provision a default network, enter it in that local header before building.
Your actual password has not been included in this archive.

Voice: see ai-server/VOICE.md. Install voice-requirements.txt in a virtual
environment and download the Piper voice as documented before starting.
The large neural model weights and installed Python packages are excluded.
LM Studio and voice-server LAN addresses can be changed in firmware/main/
odin_ai.c and odin_voice.h. Existing LAN addresses are retained as configured.

Excluded: build artifacts, backups, caches, virtual environments, private
credentials, and voice model weights. No firmware binaries containing the
provisioned password are included. Dependencies retain their original licenses.
