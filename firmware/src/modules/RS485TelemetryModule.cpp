#include "modules/RS485TelemetryModule.h"
#include <Arduino.h>

// RS485 Hardware Definitions for RAK5802 in WisBlock IO Slot
#define RS485_SERIAL       Serial2    // Hardware UART2 on RAK11310 RP2040 for IO Slot
#define RS485_DE_PIN       WB_IO1     // Driver Enable / Receiver Enable control pin
#define RS485_BAUD         9600

// Modbus Slave Addresses
#define YOSEMITECH_SLAVE_ID 0x01
#define DFROBOT_SLAVE_ID    0x02

// Modbus Function Codes
#define READ_HOLDING_REGS   0x03

RS485TelemetryModule::RS485TelemetryModule()
    : SinglePortModule("RS485Telem", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("RS485Telem")
{
    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);

    RS485_SERIAL.begin(RS485_BAUD);
    setInterval(60 * 1000);
}

uint16_t RS485TelemetryModule::calculateCRC16(const uint8_t *buffer, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool RS485TelemetryModule::sendModbusQuery(uint8_t slaveId, uint8_t funcCode, uint16_t startReg, uint16_t regCount,
                                           uint8_t *responseBuf, size_t &responseLen)
{
    uint8_t request[8];
    request[0] = slaveId;
    request[1] = funcCode;
    request[2] = (startReg >> 8) & 0xFF;
    request[3] = startReg & 0xFF;
    request[4] = (regCount >> 8) & 0xFF;
    request[5] = regCount & 0xFF;

    uint16_t crc = calculateCRC16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    while (RS485_SERIAL.available())
        RS485_SERIAL.read();

    digitalWrite(RS485_DE_PIN, HIGH);
    delayMicroseconds(50);
    RS485_SERIAL.write(request, sizeof(request));
    RS485_SERIAL.flush();
    digitalWrite(RS485_DE_PIN, LOW);

    responseLen = 0;
    unsigned long startTime = millis();
    uint16_t expectedBytes = 5 + (regCount * 2);

    while (millis() - startTime < 300) {
        while (RS485_SERIAL.available() > 0 && responseLen < 64) {
            responseBuf[responseLen++] = RS485_SERIAL.read();
        }
        if (responseLen >= expectedBytes)
            break;
    }

    if (responseLen < 5)
        return false;
    uint16_t rxCrc = responseBuf[responseLen - 2] | (responseBuf[responseLen - 1] << 8);
    return (rxCrc == calculateCRC16(responseBuf, responseLen - 2));
}

float RS485TelemetryModule::bytesToFloat(const uint8_t *bytes)
{
    union {
        float f;
        uint8_t b[4];
    } value;
    value.b[3] = bytes[0];
    value.b[2] = bytes[1];
    value.b[1] = bytes[2];
    value.b[0] = bytes[3];
    return value.f;
}

void RS485TelemetryModule::readYosemitechSonde()
{
    uint8_t response[64];
    size_t len = 0;

    if (sendModbusQuery(YOSEMITECH_SLAVE_ID, READ_HOLDING_REGS, 0x2600, 12, response, len)) {
        float temp = bytesToFloat(&response[3]);
        float pH = bytesToFloat(&response[7]);
        float dissolvedO2 = bytesToFloat(&response[11]);
        float turbidity = bytesToFloat(&response[15]);
        float cond = bytesToFloat(&response[19]);

        LOG_INFO("Y4000 SONDE: Temp=%.2fC | pH=%.2f | DO=%.2fmg/L | Turb=%.2fNTU | Cond=%.1fuS/cm", temp, pH, dissolvedO2,
                 turbidity, cond);
    } else {
        LOG_WARN("Y4000 SONDE: Modbus Timeout / CRC Error");
    }
}

void RS485TelemetryModule::readDFRobotWeather()
{
    uint8_t response[64];
    size_t len = 0;

    if (sendModbusQuery(DFROBOT_SLAVE_ID, READ_HOLDING_REGS, 0x0000, 6, response, len)) {
        uint16_t rawWindSpeed = (response[3] << 8) | response[4];
        uint16_t rawWindDir = (response[5] << 8) | response[6];
        uint16_t rawRainfall = (response[7] << 8) | response[8];
        uint16_t rawSolar = (response[9] << 8) | response[10];

        float windSpeed = rawWindSpeed / 10.0f;
        float windDir = rawWindDir / 10.0f;
        float rain = rawRainfall / 10.0f;
        float solar = rawSolar * 1.0f;

        LOG_INFO("SEN0657 WEATHER: Wind=%.1fm/s @ %.1f° | Rain=%.1fmm | Solar=%.0fW/m2", windSpeed, windDir, rain, solar);
    } else {
        LOG_WARN("SEN0657 WEATHER: Modbus Timeout / CRC Error");
    }
}

int32_t RS485TelemetryModule::runOnce()
{
    readYosemitechSonde();
    delay(100);
    readDFRobotWeather();

    return 60 * 1000;
}

RS485TelemetryModule *rs485TelemetryModule;