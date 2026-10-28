# ECE 455 Water-Quality Logger

This repository contains the firmware and dashboard for a low-cost, open-source water-quality monitoring system built around Meshtastic LoRa mesh networking. The target sensor node is a RAK WisBlock RAK11310 (RP2040 with SX1262 LoRa radio). It collects readings from a Yosemitech Y4000 water-quality sonde, a DFRobot SEN0657 weather station, and a DS18B20 temperature string, then broadcasts telemetry through the mesh to a base node.

The RAK11310 firmware is based on Meshtastic. The custom sensor integration is in `src/modules/RS485TelemetryModule.cpp` and `src/DriverDS18B20.cpp`; board-specific pins and timing are in `variants/rp2040/rak11310/variant.h`. The local dashboard is `examples/app.py`; it stores telemetry in SQLite and presents nodes, sensor readings, and map views in a browser.

## Hardware

- RAK WisBlock RAK11310 board with SX1262 LoRa radio
- RAK5802 RS485 interface and RS485 sensors (Y4000 sonde and SEN0657 weather station)
- DS18B20 probes on the temperature-string OneWire bus
- RAK12501 GNSS module
- RAK12002 RTC module
- LoRa antenna matched to the board and regional frequency
- Battery, enclosure, connectors, and sensor wiring suitable for the field environment

The RAK11310 pin assignments are board-specific. Do not change radio, GPS, or sensor GPIO assignments unless the physical wiring and board configuration are changed with them. Current setup has the RAK12501 on sensor slot E, RAK5802 on IO slot B, and RAK12002 on sensor slot B

## Development setup

1. Install Git and Visual Studio Code.
2. In VS Code, install the **PlatformIO IDE** extension. Alternatively, install PlatformIO Core and ensure the `pio` command is available in a terminal.
3. Clone this repository and open the `firmware` directory (the directory containing the top-level `platformio.ini`) in VS Code.
4. Connect the RAK11310 by USB. On Windows, check its COM port in Device Manager or with `pio device list`.
5. Let PlatformIO install the toolchains and libraries on the first build. A working internet connection is needed for this initial setup.

Commands below are run from the `firmware` directory.

## Build and flash

Build the production sensor firmware:

```powershell
pio run -e rak11310
```

Flash a connected RAK11310:

```powershell
pio run -e rak11310 -t upload
```

To specify the board's COM port explicitly:

```powershell
pio run -e rak11310 -t upload --upload-port COM5
```

If upload does not find the board, put it into its USB bootloader/DFU mode and retry. The `rak11310` environment uses `picotool` for upload. Do not use `erase_and_flash` or erase the board unless a full erase is intended.

The RAK11310 debug serial port is configured for 115200 baud. Monitor it with:

```powershell
pio device monitor -b 115200 --port COM5
```

Omit `--port COM5` if PlatformIO can select the correct serial device automatically.

## RAK11310 parameters

Edit `variants/rp2040/rak11310/variant.h` for board-specific compile-time settings. After changing a setting, rebuild and reflash the firmware; these values are not runtime settings.

| Setting | Effect |
| --- | --- |
| `USERPREFS_CONFIG_DEVICE_TELEM_UPDATE_INTERVAL` | Interval for Meshtastic's built-in **device metrics** broadcast (such as battery/device status). This is separate from the water logger's sensor cycle. |
| `RS485_SENSOR_WARMUP_MS` | Delay after powering the external sensor rail before polling the RS485 sensors. Increase only if a sensor's documentation or measurements show it needs more startup time. |
| `RS485_SENSOR_POWER_ON_DURATION_MS` | How long the external sensor rail stays on for the RS485 measurements and DS18B20 read. It must be at least as long as the warmup period. |
| `RS485_SENSOR_POWER_OFF_DURATION_MS` |  Delay after a sensor read before the next sampling/transmission cycle. Set this to `30UL * 60UL * 1000UL` for a 30-minute field cycle. The Meshtastic node and radio remain running to forward mesh traffic. |
| `RS485_SENSOR_INITIAL_DELAY_MS` | Delay before the first sensor cycle after startup, allowing the mesh stack to initialize. |
| `RS485_SENSOR_POWER_PIN` |  GPIO controlling the external sensor rail. Keep this separate from `PIN_GPS_EN` (`WB_IO2`) so sensor power switching does not disable the GNSS module. |
| `PIN_GPS_EN` | GPS power-enable rail used by the RAK12501 integration. |
| `RAK11310_LORA_TEST_MODE` | Synthetic telemetry test switch. Leave disabled for production. The dedicated `rak11310-lora-test` build environment enables it without editing the default environment. |
| `RAK11310_LORA_TEST_INTERVAL_MS` | Period between synthetic LoRa test broadcasts when test mode is enabled. |

The built-in device-metrics interval and the sensor sample interval serve different purposes. Adjust the sensor cycle with `RS485_SENSOR_POWER_OFF_DURATION_MS`; changing the device-metrics interval alone does not change how often the sonde, weather station, or temperature string is read.

Other values in `variant.h` set physical pin mappings, battery sensing calibration, and radio configuration. Change them only when adapting the firmware to a different board revision or wiring arrangement.


## Dashboard

`examples/app.py` connects to a Meshtastic node over USB serial and serves a local web dashboard at `http://127.0.0.1:5000`. Use a node connected to the computer as the base; remote nodes send their telemetry through the mesh to it.

Python 3.8 or newer is recommended. The script checks for and installs its Python dependencies (`PyPubSub`, `meshtastic`, and `Flask`) when it starts. From the firmware directory:

```powershell
$env:MESHTASTIC_SERIAL_PORT = "COM5"
python examples\app.py
```

Set `MESHTASTIC_SERIAL_PORT` to the base node's actual port. If it is omitted, the Meshtastic client attempts automatic port detection. The dashboard uses `examples\water_monitor.db` by default. To use a different SQLite database:

```powershell
$env:WATER_MONITOR_DB = "C:\path\to\water_monitor.db"
python examples\app.py
```

The dashboard updates as telemetry arrives. A node needs a valid GPS location before it can be shown at its sensor location on the map.

## Troubleshooting

- **Build cannot find `pio`:** open the PlatformIO terminal in VS Code or install PlatformIO Core and add it to `PATH`.
- **Upload cannot find the board:** check the COM port, close any serial monitor using it, and enter USB bootloader/DFU mode.
- **No dashboard data:** confirm the dashboard is connected to the base node's port and that remote nodes are transmitting telemetry.

