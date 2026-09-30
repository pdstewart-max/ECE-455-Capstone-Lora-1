#include "configuration.h"

#if defined(RAK11310)

#include "modules/RS485TelemetryModule.h"

#include "MeshService.h"
#include "Router.h"
#include "gps/RTC.h"

#include <ArduinoRS485.h>
#include <cstring>

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
constexpr uint32_t MODBUS_RESPONSE_TIMEOUT_MS = 3000;
constexpr uint32_t POLL_INTERVAL_MS = 60 * 1000;
} // namespace

RS485TelemetryModule::RS485TelemetryModule()
    : concurrency::OSThread("RS485Telemetry"),
      ProtobufModule("RS485Telemetry", meshtastic_PortNum_TELEMETRY_APP, &meshtastic_Telemetry_msg)
{
    // Power on 3V3_S rail for WisBlock Slot
    pinMode(WB_IO2, OUTPUT);
    digitalWrite(WB_IO2, HIGH);
    delay(100);

    // Initialize ArduinoRS485 using native Serial1
    RS485.setPins(RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN);
    RS485.begin(RS485_BAUD);
    RS485.receive();

    // LOG_INFO("RS485 transport=ArduinoRS485 UART=Serial1 TX=0 RX=1 DE=6 RE=-1");
    setIntervalFromNow(POLL_INTERVAL_MS);
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

    // LOG_DEBUG("Modbus TX id=0x%x reg=0x%04x count=%u", static_cast<unsigned>(slaveId), static_cast<unsigned>(startRegister), static_cast<unsigned>(count));

    // 3. Transmit request
    RS485.beginTransmission();
    RS485.write(request, sizeof(request));
    RS485.endTransmission();

    // 4. Wait ~1.2 ms (12 bit-times at 9600 baud) for the hardware UART shift register to clear
    delayMicroseconds(1200);

    // 5. Switch RS485 transceiver to receive mode
    RS485.receive();

    // 6. Clear pin-toggle noise without delaying
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
        // LOG_DEBUG("Modbus timeout id=0x%x reg=0x%04x wait=%u", static_cast<unsigned>(slaveId), static_cast<unsigned>(startRegister), static_cast<unsigned>(millis() - startedAt));
        return false;
    }
    if (responseLength < 3) {
        // LOG_DEBUG("Modbus partial header id=0x%x len=%u", static_cast<unsigned>(slaveId), static_cast<unsigned>(responseLength));
        return false;
    }
    if (frameLength == 5 && responseLength == frameLength) {
        // LOG_WARN("Modbus exception id=0x%x code=0x%x", static_cast<unsigned>(response[0]), static_cast<unsigned>(response[2]));
        return false;
    }
    if (responseLength != expectedLength || response[1] != MODBUS_READ_HOLDING_REGISTERS || response[2] != count * 2) {
        // LOG_DEBUG("Modbus bad frame id=0x%x fn=0x%x bytes=%u len=%u expected=%u", static_cast<unsigned>(response[0]), static_cast<unsigned>(response[1]), static_cast<unsigned>(response[2]), static_cast<unsigned>(responseLength), static_cast<unsigned>(expectedLength));
        return false;
    }

    const uint16_t receivedCrc = static_cast<uint16_t>(response[responseLength - 2]) |
                                 (static_cast<uint16_t>(response[responseLength - 1]) << 8);
    if (receivedCrc != calculateCRC16(response, responseLength - 2)) {
        // LOG_DEBUG("Modbus CRC error id=0x%x received=0x%04x", static_cast<unsigned>(slaveId), static_cast<unsigned>(receivedCrc));
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

    // Primary output for Yosemitech Y4000 Sonde
    LOG_INFO("Y4000 Water Quality -> DO: %.2f mg/L | Turbidity: %.2f NTU | Conductivity: %.3f mS/cm | pH: %.2f | Temp: %.2f C | ORP: %.1f mV | Chlorophyll: %.2f ug/L | BGA: %.2f cells/mL",
             waterMetrics.dissolved_oxygen, waterMetrics.turbidity, waterMetrics.electrical_conductivity,
             waterMetrics.ph, temperature, waterMetrics.orp, decodeY4000Float(&registers[12]), decodeY4000Float(&registers[14]));
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

// Handle received telemetry messages (not used in this module)
bool RS485TelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &, meshtastic_Telemetry *)
{
    return false;
}

int32_t RS485TelemetryModule::runOnce()
{
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
        // LOG_WARN("Y4000 Modbus read failed");
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
        // LOG_WARN("SEN0657 Modbus read failed");
    }

    return POLL_INTERVAL_MS;
}

RS485TelemetryModule *rs485TelemetryModule;

#endif