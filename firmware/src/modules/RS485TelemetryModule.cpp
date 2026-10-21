#include "configuration.h"

#if defined(RAK11310)

#include "modules/RS485TelemetryModule.h"

#include "MeshService.h"
#include "Router.h"
#include "gps/RTC.h"
#include "mesh/Throttle.h"
#include "UptimeClock.h"

#include <ArduinoRS485.h>
#include <cstring>
#include <limits>

#include "DriverDS18B20.h"

// Pin definitions:
// GPIO0 = TX, GPIO1 = RX (implicitly handled by Serial1), WB_IO1 (GPIO6) = DE/RE
constexpr uint8_t RS485_TX_PIN = 0;
constexpr uint8_t RS485_DE_PIN = WB_IO1;
constexpr int8_t RS485_RE_PIN = -1; // -1 disables RE pin override since DE and /RE are tied together
constexpr uint32_t RS485_BAUD = 9600;

namespace
{
constexpr uint8_t Y4000_SLAVE_ID = 1;
constexpr uint8_t SEN0657_SLAVE_ID = 2;
constexpr uint16_t MODBUS_READ_HOLDING_REGISTERS = 0x03;
constexpr uint16_t MODBUS_WRITE_HOLDING_REGISTERS = 0x06;
constexpr int16_t INVALID_TEMPERATURE = std::numeric_limits<int16_t>::min();
constexpr size_t LORA_TEST_TEMP_SENSOR_COUNT = 14;
// Bound blocking time on the cooperative mesh loop if a sensor or cable is absent.
constexpr uint32_t MODBUS_RESPONSE_TIMEOUT_MS = 500;
static_assert(RS485_SENSOR_WARMUP_MS <= RS485_SENSOR_POWER_ON_DURATION_MS,
              "sensor warm-up must fit within the configured power-on duration");
static_assert(RS485_SENSOR_POWER_ON_DURATION_MS <= 0x7FFFFFFFUL &&
                  RS485_SENSOR_POWER_OFF_DURATION_MS <= 0x7FFFFFFFUL,
              "sensor power durations must be at most 24.8 days");
} // namespace

RS485TelemetryModule::RS485TelemetryModule()
    : concurrency::OSThread("RS485Telemetry"),
      ProtobufModule("RS485Telemetry", meshtastic_PortNum_TELEMETRY_APP, &meshtastic_Telemetry_msg)
{
#if RAK11310_LORA_TEST_MODE
    LOG_WARN("LoRa telemetry test mode enabled; external sensor polling is disabled");
#else
    pinMode(RS485_SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(RS485_SENSOR_POWER_PIN, RS485_SENSOR_POWER_OFF);

    // Initialize ArduinoRS485 using native Serial1
    RS485.setPins(RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN);
    RS485.begin(RS485_BAUD);
    RS485.receive();

    // Initialize DallasTemperature library for DS18B20 sensor
    DriverDS18B20::init();

    LOG_INFO("RS485 transport=ArduinoRS485 UART=Serial1 TX=0 RX=1 DE=6 RE=-1");
#endif
    setIntervalFromNow(RS485_SENSOR_INITIAL_DELAY_MS);
}

uint16_t RS485TelemetryModule::calculateCRC16(const uint8_t *buffer, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

// bool RS485TelemetryModule::writeRegister(uint8_t slaveId, uint16_t registerAddr, uint16_t value) {
//     delay(20);

//     while (RS485.available()) // Flush stale bytes from buffer
//         RS485.read();

//     uint8_t request[8] = {
//         slaveId,
//         MODBUS_WRITE_HOLDING_REGISTERS,
//         static_cast<uint8_t>(registerAddr >> 8),
//         static_cast<uint8_t>(registerAddr),
//         static_cast<uint8_t>(value >> 8),
//         static_cast<uint8_t>(value),
//         0, 0
//     };
//     uint16_t crc = calculateCRC16(request, 6);
//     request[6] = static_cast<uint8_t>(crc);
//     request[7] = static_cast<uint8_t>(crc >> 8);

//     // Transmit request
//     RS485.beginTransmission();
//     RS485.write(request, sizeof(request));
//     RS485.endTransmission(); // THIS IS A BLOCKING FUNCTION
//     RS485.receive();

//     while (RS485.available())
//         RS485.read();

//     uint8_t response[8];
//     size_t responseLength = 0;
//     const uint32_t startedAt = millis();

//     while (static_cast<uint32_t>(millis() - startedAt) < MODBUS_RESPONSE_TIMEOUT_MS) {
//         while (RS485.available() && responseLength < sizeof(response)) {
//             response[responseLength++] = static_cast<uint8_t>(RS485.read());
//         }
//         if (responseLength == sizeof(response)) break;
//     }

//     // 4. Validate full length received
//     if (responseLength < 8) {
//         LOG_DEBUG("Modbus timeout or incomplete response: %u/8 bytes", responseLength);
//         return false;
//     }

//     // 5. Check for Exception response (Function Code | 0x80)
//     if (response[0] == slaveId && response[1] == (0x06 | 0x80)) {
//         LOG_WARN("Modbus exception code: 0x%02X", response[2]);
//         return false;
//     }

//     // 6. Validate CRC
//     const uint16_t receivedCrc = static_cast<uint16_t>(response[6]) | (static_cast<uint16_t>(response[7]) << 8);
//     if (receivedCrc != calculateCRC16(response, 6)) {
//         LOG_DEBUG("Modbus CRC error");
//         return false;
//     }

//     // 7. Verify Echo (Slave ID & Function Code match)
//     if (response[0] != slaveId || response[1] != 0x06) {
//         LOG_DEBUG("Modbus echo mismatch");
//         return false;
//     }

//     return true; // Write successfully acknowledged by slave
// }

bool RS485TelemetryModule::readRegisters(uint8_t slaveId, uint16_t startRegister, uint16_t count, uint16_t *registers)
{
    if (count == 0 || count > 16)
        return false;

    // Small inter-frame delay to allow the RS485 bus/slave to settle
    delay(20);

    // 1. Purge stale bytes before sending new request
    while (RS485.available())
        RS485.read();

    // 2. Build Modbus request frame
    uint8_t request[8] = {
        slaveId,
        MODBUS_READ_HOLDING_REGISTERS,
        static_cast<uint8_t>(startRegister >> 8),
        static_cast<uint8_t>(startRegister),
        static_cast<uint8_t>(count >> 8),
        static_cast<uint8_t>(count),
        0, 0
    };
    uint16_t crc = calculateCRC16(request, 6);
    request[6] = static_cast<uint8_t>(crc);
    request[7] = static_cast<uint8_t>(crc >> 8);

    LOG_DEBUG("Modbus TX id=0x%x reg=0x%04x count=%u", static_cast<unsigned>(slaveId), static_cast<unsigned>(startRegister), static_cast<unsigned>(count));

    // 3. Transmit request
    RS485.beginTransmission();
    RS485.write(request, sizeof(request));
    RS485.endTransmission(); // THIS IS A BLOCKING FUNCTION
    
    // Switch RS485 transceiver to receive mode
    RS485.receive();

    // Extra wait time for hardware buffer to clear a frame (~12 bits @ 9600 baud) though not necessary because of RS485.endTransmission()
    // delayMicroseconds(1200);
    
    // Clear pin-toggle noise without delaying
    while (RS485.available())
        RS485.read();

    // 7. Parse response frame
    uint8_t response[37];
    size_t responseLength = 0;
    const size_t expectedLength = 5 + count * 2;
    size_t frameLength = 0;
    const uint32_t startedAt = millis();

    while (static_cast<uint32_t>(millis() - startedAt) < MODBUS_RESPONSE_TIMEOUT_MS) {
        while (RS485.available() && responseLength < sizeof(response)) {
            const uint8_t byte = static_cast<uint8_t>(RS485.read());

            if (responseLength == 0) {
                if (byte == slaveId)
                    response[responseLength++] = byte;
                continue;
            }
            if (responseLength == 1) {
                if (byte == MODBUS_READ_HOLDING_REGISTERS || byte == (MODBUS_READ_HOLDING_REGISTERS | 0x80))
                    response[responseLength++] = byte;
                else
                    responseLength = (byte == slaveId) ? 1 : 0;
                if (responseLength == 1)
                    response[0] = byte;
                continue;
            }
            if (responseLength == 2) {
                const bool isException = (response[1] == (MODBUS_READ_HOLDING_REGISTERS | 0x80));
                if (!isException && byte != count * 2) {
                    responseLength = (byte == slaveId) ? 1 : 0;
                    if (responseLength == 1)
                        response[0] = byte;
                    continue;
                }
                response[responseLength++] = byte;
                frameLength = isException ? 5 : expectedLength;
                continue;
            }
            response[responseLength++] = byte;
            if (responseLength == frameLength)
                break;
        }
        if (frameLength != 0 && responseLength == frameLength)
            break;
    }

    if (responseLength == 0) {
        LOG_DEBUG("Modbus timeout id=0x%x reg=0x%04x wait=%u", static_cast<unsigned>(slaveId), static_cast<unsigned>(startRegister), static_cast<unsigned>(millis() - startedAt));
        return false;
    }
    if (responseLength < 3) {
        LOG_DEBUG("Modbus partial header id=0x%x len=%u", static_cast<unsigned>(slaveId), static_cast<unsigned>(responseLength));
        return false;
    }
    if (frameLength == 5 && responseLength == frameLength) {
        LOG_WARN("Modbus exception id=0x%x code=0x%x", static_cast<unsigned>(response[0]), static_cast<unsigned>(response[2]));
        return false;
    }
    if (responseLength != expectedLength || response[1] != MODBUS_READ_HOLDING_REGISTERS || response[2] != count * 2) {
        LOG_DEBUG("Modbus bad frame id=0x%x fn=0x%x bytes=%u len=%u expected=%u", static_cast<unsigned>(response[0]), static_cast<unsigned>(response[1]), static_cast<unsigned>(response[2]), static_cast<unsigned>(responseLength), static_cast<unsigned>(expectedLength));
        return false;
    }

    const uint16_t receivedCrc = static_cast<uint16_t>(response[responseLength - 2]) |
                                 (static_cast<uint16_t>(response[responseLength - 1]) << 8);
    if (receivedCrc != calculateCRC16(response, responseLength - 2)) {
        LOG_DEBUG("Modbus CRC error id=0x%x received=0x%04x", static_cast<unsigned>(slaveId), static_cast<unsigned>(receivedCrc));
        return false;
    }

    for (uint16_t i = 0; i < count; i++) {
        registers[i] = (static_cast<uint16_t>(response[3 + i * 2]) << 8) | response[4 + i * 2];
    }
    return true;
}

float RS485TelemetryModule::decodeY4000Float(const uint16_t *registers)
{
    const uint32_t bits = (static_cast<uint32_t>(registers[1] & 0xFF) << 24) |
                          (static_cast<uint32_t>(registers[1] >> 8) << 16) |
                          (static_cast<uint32_t>(registers[0] & 0xFF) << 8) | (registers[0] >> 8);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool RS485TelemetryModule::readY4000(meshtastic_SoilWaterMetrics &waterMetrics, float &temperature)
{
    uint16_t registers[16];
    if (!readRegisters(Y4000_SLAVE_ID, 0x2601, 16, registers))
        return false;

    waterMetrics.has_dissolved_oxygen = true;
    waterMetrics.dissolved_oxygen = decodeY4000Float(&registers[0]);
    waterMetrics.has_turbidity = true;
    waterMetrics.turbidity = decodeY4000Float(&registers[2]);
    waterMetrics.has_electrical_conductivity = true;
    waterMetrics.electrical_conductivity = decodeY4000Float(&registers[4]);
    waterMetrics.has_ph = true;
    waterMetrics.ph = decodeY4000Float(&registers[6]);
    temperature = decodeY4000Float(&registers[8]);
    waterMetrics.has_orp = true;
    waterMetrics.orp = decodeY4000Float(&registers[10]);
    const float chlorophyll = decodeY4000Float(&registers[12]);
    const float bga = decodeY4000Float(&registers[14]);
    waterMetrics.has_chlorophyll = true;
    waterMetrics.chlorophyll = chlorophyll;
    waterMetrics.has_bga = true;
    waterMetrics.bga = bga;

    // Primary output for Yosemitech Y4000 Sonde
    LOG_INFO("Y4000 Water Quality -> DO: %.2f mg/L | Turbidity: %.2f NTU | Conductivity: %.3f mS/cm | pH: %.2f | Temp: %.2f C | ORP: %.1f mV | Chlorophyll: %.2f ug/L",
             waterMetrics.dissolved_oxygen, waterMetrics.turbidity, waterMetrics.electrical_conductivity,
             waterMetrics.ph, temperature, waterMetrics.orp, chlorophyll);
    LOG_INFO("Y4000 BGA -> %.2f cells/mL", bga);
    return true;
}

bool RS485TelemetryModule::readSEN0657(meshtastic_EnvironmentMetrics &weatherMetrics, uint16_t &rainfallTenthsMm)
{
    uint16_t wind[4];
    uint16_t humidityTemperature[2];
    uint16_t pressureLight[3];
    uint16_t rainfall[1];

    if (!readRegisters(SEN0657_SLAVE_ID, 0x01F4, 4, wind) || !readRegisters(SEN0657_SLAVE_ID, 0x01F8, 2, humidityTemperature) ||
        !readRegisters(SEN0657_SLAVE_ID, 0x01FD, 3, pressureLight) || !readRegisters(SEN0657_SLAVE_ID, 0x0201, 1, rainfall)) {
        return false;
    }

    weatherMetrics.has_wind_speed = true;
    weatherMetrics.wind_speed = wind[0] / 10.0f;
    weatherMetrics.has_wind_direction = true;
    weatherMetrics.wind_direction = wind[3];
    weatherMetrics.has_relative_humidity = true;
    weatherMetrics.relative_humidity = humidityTemperature[0] / 10.0f;
    weatherMetrics.has_temperature = true;
    weatherMetrics.temperature = static_cast<int16_t>(humidityTemperature[1]) / 10.0f;
    weatherMetrics.has_barometric_pressure = true;
    weatherMetrics.barometric_pressure = pressureLight[0] * 1.0f;
    weatherMetrics.has_lux = true;
    weatherMetrics.lux = static_cast<float>((static_cast<uint32_t>(pressureLight[1]) << 16) | pressureLight[2]);
    rainfallTenthsMm = rainfall[0];
    weatherMetrics.has_rainfall_1h = true;
    weatherMetrics.rainfall_1h = rainfallTenthsMm / 10.0f;

    // Primary output for DFRobot SEN0657 Weather Station
    LOG_INFO("SEN0657 Weather Station -> Temp: %.1f C | Humidity: %.1f %% | Pressure: %.1f hPa | Wind Speed: %.1f m/s | Wind Dir: %u deg | Lux: %.0f | Rainfall: %.1f mm",
             weatherMetrics.temperature, weatherMetrics.relative_humidity, weatherMetrics.barometric_pressure,
             weatherMetrics.wind_speed, weatherMetrics.wind_direction, weatherMetrics.lux, rainfallTenthsMm / 10.0f);
    return true;
}

// Send telemetry data to the mesh network
bool RS485TelemetryModule::sendTelemetry(const meshtastic_Telemetry &telemetry)
{
    meshtastic_MeshPacket *packet = allocDataProtobuf(telemetry);
    if (!packet)
        return false;

    packet->to = NODENUM_BROADCAST;
    packet->decoded.want_response = false;
    packet->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    service->sendToMesh(packet, RX_SRC_LOCAL, true);
    return true;
}

bool RS485TelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &packet, meshtastic_Telemetry *telemetry)
{
#if RAK11310_LORA_TEST_MODE
    if (packet.from != 0) {
        LOG_INFO("LoRa telemetry RX from !%08x variant=%u", static_cast<unsigned>(packet.from),
                 static_cast<unsigned>(telemetry->which_variant));
        if (telemetry->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
            const auto &metrics = telemetry->variant.environment_metrics;
            if (metrics.has_temperature && metrics.temperature == 42.0f && metrics.has_relative_humidity &&
                metrics.relative_humidity == 42.0f && metrics.has_barometric_pressure &&
                metrics.barometric_pressure == 4242.0f) {
                LOG_INFO("LoRa test sample received from !%08x: temp=%.1f C humidity=%.1f%% pressure=%.1f hPa",
                         static_cast<unsigned>(packet.from), metrics.temperature, metrics.relative_humidity,
                         metrics.barometric_pressure);
            }
        } else if (telemetry->which_variant == meshtastic_Telemetry_temp_string_metrics_tag) {
            const auto &temperatures = telemetry->variant.temp_string_metrics.temperatures;
            if (temperatures.size != LORA_TEST_TEMP_SENSOR_COUNT * sizeof(int16_t)) {
                LOG_WARN("LoRa test temperature string from !%08x has %u bytes; expected %u",
                         static_cast<unsigned>(packet.from), static_cast<unsigned>(temperatures.size),
                         static_cast<unsigned>(LORA_TEST_TEMP_SENSOR_COUNT * sizeof(int16_t)));
            } else {
                const auto *bytes = temperatures.bytes;
                const int16_t first = static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) |
                                                           (static_cast<uint16_t>(bytes[1]) << 8));
                const size_t lastOffset = (LORA_TEST_TEMP_SENSOR_COUNT - 1) * sizeof(int16_t);
                const int16_t last = static_cast<int16_t>(static_cast<uint16_t>(bytes[lastOffset]) |
                                                          (static_cast<uint16_t>(bytes[lastOffset + 1]) << 8));
                if (first == 10 * 16 && last == 23 * 16) {
                    LOG_INFO("LoRa test temperature string received from !%08x: %u sensors, first=%.2f C last=%.2f C",
                             static_cast<unsigned>(packet.from), static_cast<unsigned>(LORA_TEST_TEMP_SENSOR_COUNT),
                             first / 16.0f, last / 16.0f);
                } else {
                    LOG_WARN("LoRa test temperature string from !%08x has unexpected values: first=%d last=%d",
                             static_cast<unsigned>(packet.from), static_cast<int>(first), static_cast<int>(last));
                }
            }
        }
    }
#else
    (void)packet;
    (void)telemetry;
#endif
    return false;
}

int32_t RS485TelemetryModule::runOnce()
{
#if RAK11310_LORA_TEST_MODE
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    telemetry.time = getTime();
    telemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    telemetry.variant.environment_metrics.has_temperature = true;
    telemetry.variant.environment_metrics.temperature = 42.0f;
    telemetry.variant.environment_metrics.has_relative_humidity = true;
    telemetry.variant.environment_metrics.relative_humidity = 42.0f;
    telemetry.variant.environment_metrics.has_barometric_pressure = true;
    telemetry.variant.environment_metrics.barometric_pressure = 4242.0f;
    if (sendTelemetry(telemetry)) {
        LOG_INFO("LoRa test telemetry TX from !%08x: temp=42.0 C humidity=42.0%% pressure=4242.0 hPa",
                 static_cast<unsigned>(nodeDB->getNodeNum()));
    } else {
        LOG_ERROR("Failed to allocate LoRa test telemetry packet");
    }

    uint8_t temperatureBytes[LORA_TEST_TEMP_SENSOR_COUNT * sizeof(int16_t)];
    for (size_t i = 0; i < LORA_TEST_TEMP_SENSOR_COUNT; i++) {
        const int16_t temperature = static_cast<int16_t>((10 + i) * 16);
        const uint16_t encoded = static_cast<uint16_t>(temperature);
        temperatureBytes[i * 2] = static_cast<uint8_t>(encoded);
        temperatureBytes[i * 2 + 1] = static_cast<uint8_t>(encoded >> 8);
    }

    meshtastic_Telemetry temperatureTelemetry = meshtastic_Telemetry_init_zero;
    temperatureTelemetry.time = telemetry.time;
    temperatureTelemetry.which_variant = meshtastic_Telemetry_temp_string_metrics_tag;
    temperatureTelemetry.variant.temp_string_metrics.temperatures.size = sizeof(temperatureBytes);
    std::memcpy(temperatureTelemetry.variant.temp_string_metrics.temperatures.bytes, temperatureBytes,
                sizeof(temperatureBytes));
    if (sendTelemetry(temperatureTelemetry)) {
        LOG_INFO("LoRa test temperature string TX from !%08x: 14 sensors, 10.00 C to 23.00 C",
                 static_cast<unsigned>(nodeDB->getNodeNum()));
    } else {
        LOG_ERROR("Failed to allocate LoRa test temperature string packet");
    }
    return RAK11310_LORA_TEST_INTERVAL_MS;
#else
    if (!sensorPowerOn) {
        digitalWrite(RS485_SENSOR_POWER_PIN, RS485_SENSOR_POWER_ON);
        sensorPowerOn = true;
        sensorReadComplete = false;
        sensorPowerStartedAtMs = Time::getMillis();
        LOG_DEBUG("External sensor rail enabled");
        return RS485_SENSOR_WARMUP_MS;
    }

    if (!sensorReadComplete) {
        meshtastic_SoilWaterMetrics waterMetrics = meshtastic_SoilWaterMetrics_init_zero;
        float waterTemperature = 0;
        if (readY4000(waterMetrics, waterTemperature)) {
            meshtastic_Telemetry waterTelemetry = meshtastic_Telemetry_init_zero;
            waterTelemetry.time = getTime();
            waterTelemetry.which_variant = meshtastic_Telemetry_soil_water_metrics_tag;
            waterTelemetry.variant.soil_water_metrics = waterMetrics;
            sendTelemetry(waterTelemetry);

            meshtastic_Telemetry temperatureTelemetry = meshtastic_Telemetry_init_zero;
            temperatureTelemetry.time = waterTelemetry.time;
            temperatureTelemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;
            temperatureTelemetry.variant.environment_metrics.has_temperature = true;
            temperatureTelemetry.variant.environment_metrics.temperature = waterTemperature;
            sendTelemetry(temperatureTelemetry);
        } else {
            LOG_WARN("Y4000 Modbus read failed");
        }

        meshtastic_EnvironmentMetrics weatherMetrics = meshtastic_EnvironmentMetrics_init_zero;
        uint16_t rainfallTenthsMm = 0;
        if (readSEN0657(weatherMetrics, rainfallTenthsMm)) {
            meshtastic_Telemetry weatherTelemetry = meshtastic_Telemetry_init_zero;
            weatherTelemetry.time = getTime();
            weatherTelemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;
            weatherTelemetry.variant.environment_metrics = weatherMetrics;
            sendTelemetry(weatherTelemetry);
        } else {
            LOG_WARN("SEN0657 Modbus read failed");
        }
        sensorReadComplete = true;
    }

    if (!Throttle::hasElapsed(sensorPowerStartedAtMs, RS485_SENSOR_POWER_ON_DURATION_MS)) {
        const uint32_t elapsedMs = Time::getMillis() - sensorPowerStartedAtMs;
        return RS485_SENSOR_POWER_ON_DURATION_MS - elapsedMs;
    }

    float temps[DS18B20_SENSOR_COUNT];
    if (DriverDS18B20::readAllTemperatures(temps)) {
        uint8_t tempBytes[DS18B20_SENSOR_COUNT * sizeof(int16_t)];
        size_t lastValidIndex = 0;
        for (size_t i = 0; i < DS18B20_SENSOR_COUNT; i++) {
            int16_t encodedTemperature = INVALID_TEMPERATURE;
            if (temps[i] != DEVICE_DISCONNECTED_C) {
                encodedTemperature = static_cast<int16_t>(temps[i] * 16.0f);
                lastValidIndex = i;
            }
            const uint16_t encodedBits = static_cast<uint16_t>(encodedTemperature);
            tempBytes[i * 2] = static_cast<uint8_t>(encodedBits);
            tempBytes[i * 2 + 1] = static_cast<uint8_t>(encodedBits >> 8);
        }

        meshtastic_Telemetry dsTelemetry = meshtastic_Telemetry_init_zero;
        dsTelemetry.time = getTime();
        dsTelemetry.which_variant = meshtastic_Telemetry_temp_string_metrics_tag;
        const size_t encodedSize = (lastValidIndex + 1) * 2;
        dsTelemetry.variant.temp_string_metrics.temperatures.size = encodedSize;
        std::memcpy(dsTelemetry.variant.temp_string_metrics.temperatures.bytes, tempBytes, encodedSize);
        if (!sendTelemetry(dsTelemetry)) {
            LOG_ERROR("Failed to allocate DS18B20 temperature telemetry packet");
        }
    }

    digitalWrite(RS485_SENSOR_POWER_PIN, RS485_SENSOR_POWER_OFF);
    sensorPowerOn = false;
    LOG_DEBUG("External sensor rail disabled");
    return RS485_SENSOR_POWER_OFF_DURATION_MS;
#endif
}

RS485TelemetryModule *rs485TelemetryModule;

#endif