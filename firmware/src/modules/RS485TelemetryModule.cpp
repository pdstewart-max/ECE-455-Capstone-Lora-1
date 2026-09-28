#include "mesh/SinglePortModule.h"
#include "concurrency/OSThread.h"
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

class RS485TelemetryModule : public SinglePortModule, public concurrency::OSThread {
public:
    RS485TelemetryModule() 
        : SinglePortModule("RS485Telem", meshtastic_PortNum_TEXT_MESSAGE_APP), 
          concurrency::OSThread("RS485Telem") 
    {
        pinMode(RS485_DE_PIN, OUTPUT);
        digitalWrite(RS485_DE_PIN, LOW); // Set to RX mode by default

        RS485_SERIAL.begin(RS485_BAUD);
        
        setInterval(60 * 1000); // Poll sensors every 60 seconds
    }

protected:
    // Helper: Compute Modbus RTU CRC16
    uint16_t calculateCRC16(const uint8_t *buffer, uint16_t length) {
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

    // Helper: Send Modbus Request and Read Response Frame
    bool sendModbusQuery(uint8_t slaveId, uint8_t funcCode, uint16_t startReg, uint16_t regCount, uint8_t *responseBuf, size_t &responseLen) {
        uint8_t request[8];
        request[0] = slaveId;
        request[1] = funcCode;
        request[2] = (startReg >> 8) & 0xFF;
        request[3] = startReg & 0xFF;
        request[4] = (regCount >> 8) & 0xFF;
        request[5] = regCount & 0xFF;

        uint16_t crc = calculateCRC16(request, 6);
        request[6] = crc & 0xFF;        // CRC Low Byte
        request[7] = (crc >> 8) & 0xFF; // CRC High Byte

        // Clear RX Buffer
        while (RS485_SERIAL.available()) RS485_SERIAL.read();

        // Switch to Transmit Mode
        digitalWrite(RS485_DE_PIN, HIGH);
        delayMicroseconds(50);
        RS485_SERIAL.write(request, sizeof(request));
        RS485_SERIAL.flush(); // Wait for UART transmission to complete
        digitalWrite(RS485_DE_PIN, LOW); // Switch to Receive Mode

        // Read Response with 300ms Timeout
        responseLen = 0;
        unsigned long startTime = millis();
        uint16_t expectedBytes = 5 + (regCount * 2);

        while (millis() - startTime < 300) {
            while (RS485_SERIAL.available() > 0 && responseLen < 64) {
                responseBuf[responseLen++] = RS485_SERIAL.read();
            }
            if (responseLen >= expectedBytes) break;
        }

        // Validate Minimum Length and CRC
        if (responseLen < 5) return false;
        uint16_t rxCrc = responseBuf[responseLen - 2] | (responseBuf[responseLen - 1] << 8);
        return (rxCrc == calculateCRC16(responseBuf, responseLen - 2));
    }

    // Helper: Convert 4 Big-Endian Bytes to IEEE-754 Float
    float bytesToFloat(const uint8_t *b) {
        union {
            float f;
            uint8_t b[4];
        } u;
        u.b[3] = b[0];
        u.b[2] = b[1];
        u.b[1] = b[2];
        u.b[0] = b[3];
        return u.f;
    }

    // --- SENSOR 1: Yosemitech Y4000 Sonde ---
    void readYosemitechSonde() {
        uint8_t response[64];
        size_t len = 0;

        // Yosemitech registers typically store IEEE 754 32-bit floats starting at 0x2600
        if (sendModbusQuery(YOSEMITECH_SLAVE_ID, READ_HOLDING_REGS, 0x2600, 12, response, len)) {
            // Data bytes start at response[3]
            float temp        = bytesToFloat(&response[3]);
            float pH          = bytesToFloat(&response[7]);
            float dissolvedO2 = bytesToFloat(&response[11]);
            float turbidity   = bytesToFloat(&response[15]);
            float cond        = bytesToFloat(&response[19]);

            LOG_INFO("Y4000 SONDE: Temp=%.2fC | pH=%.2f | DO=%.2fmg/L | Turb=%.2fNTU | Cond=%.1fuS/cm",
                     temp, pH, dissolvedO2, turbidity, cond);
        } else {
            LOG_WARN("Y4000 SONDE: Modbus Timeout / CRC Error");
        }
    }

    // --- SENSOR 2: DFRobot SEN0657 Weather Station ---
    void readDFRobotWeather() {
        uint8_t response[64];
        size_t len = 0;

        // SEN0657 registers (Holding Registers 0x0000 onward: Wind Speed, Wind Dir, Rain, Solar)
        if (sendModbusQuery(DFROBOT_SLAVE_ID, READ_HOLDING_REGS, 0x0000, 6, response, len)) {
            // Values are returned as 16-bit unsigned integers scaled by 10 or 100
            uint16_t rawWindSpeed = (response[3] << 8) | response[4];
            uint16_t rawWindDir   = (response[5] << 8) | response[6];
            uint16_t rawRainfall  = (response[7] << 8) | response[8];
            uint16_t rawSolar     = (response[9] << 8) | response[10];

            float windSpeed = rawWindSpeed / 10.0f;  // m/s
            float windDir   = rawWindDir / 10.0f;    // Degrees
            float rain      = rawRainfall / 10.0f;   // mm
            float solar     = rawSolar * 1.0f;       // W/m²

            LOG_INFO("SEN0657 WEATHER: Wind=%.1fm/s @ %.1f° | Rain=%.1fmm | Solar=%.0fW/m2",
                     windSpeed, windDir, rain, solar);
        } else {
            LOG_WARN("SEN0657 WEATHER: Modbus Timeout / CRC Error");
        }
    }

    virtual int32_t runOnce() override {
        // Query both devices sequentially on the shared RS485 bus
        readYosemitechSonde();
        delay(100); // Inter-frame delay for bus settling
        readDFRobotWeather();

        return 60 * 1000;
    }
};

RS485TelemetryModule *rs485TelemetryModule;