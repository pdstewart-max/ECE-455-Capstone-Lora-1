// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

// Define I2C pins to ensure correct usage of both ports
#define I2C_SDA 20
#define I2C_SCL 21
#define I2C_SDA1 2
#define I2C_SCL1 3

#define LED_POWER PIN_LED1
#define ledOff(pin) pinMode(pin, INPUT)

#define BUTTON_PIN 9
#define BUTTON_NEED_PULLUP
// #define EXT_NOTIFY_OUT 4

#define BATTERY_PIN 26
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION
// ratio of voltage divider = 3.0 (R17=200k, R18=100k)
#define ADC_MULTIPLIER 1.84

#define DETECTION_SENSOR_EN 28

// Broadcast device metrics over LoRa every two minutes.
#define USERPREFS_CONFIG_DEVICE_TELEMETRY_ENABLED true
#define USERPREFS_CONFIG_DEVICE_TELEM_UPDATE_INTERVAL 120 // time in seconds between telemetry broadcasts

#define HAS_GPS 1
#define GPS_SERIAL_PORT Serial1
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX
#define PIN_GPS_EN WB_IO2

// External sensor-rail switch; keep separate from the GPS enable rail.
#define RS485_SENSOR_POWER_PIN WB_IO6
#define RS485_SENSOR_POWER_ON HIGH
#define RS485_SENSOR_POWER_OFF LOW
static_assert(RS485_SENSOR_POWER_PIN != PIN_GPS_EN, "sensor rail switch must not control GPS power");

// Sensor rail duty cycle; adjust these durations for the deployment.
#define RS485_SENSOR_WARMUP_MS (5UL * 1000UL) // 5 seconds is sufficient for the DFRobot SEN0657 Weather Station to stabilize
#define RS485_SENSOR_POWER_ON_DURATION_MS (15UL * 1000UL)   // 15 seconds is sufficient for the DFRobot SEN0657 Weather Station to complete a measurement cycle
#define RS485_SENSOR_POWER_OFF_DURATION_MS (2UL * 60UL * 1000UL) // Two minutes between sensor measurement cycles
#define RS485_SENSOR_INITIAL_DELAY_MS (1UL * 1000UL) // 1 second delay before the first sensor power-on to allow the mesh stack to initialize

#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// RAK BSP somehow uses SPI1 instead of SPI0
#define HW_SPI1_DEVICE
#define LORA_SCK (10u)
#define LORA_MOSI (11u)
#define LORA_MISO (12u)
#define LORA_CS (13u)

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 14
#define LORA_DIO1 29
#define LORA_DIO2 15
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_POWER_EN 25
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// Ethernet and WiFi support are intentionally disabled for this minimal sensor node.
#define HAS_ETHERNET 0
#define PIN_ETHERNET_RESET 7 // IO3
#define PIN_ETHERNET_SS 17
#define ETH_SPI_PORT SPI

#define PIN_ETH_POWER_EN 22
