#include <Arduino.h>
#include <ArduinoRS485.h>
#include <cstring>

// Match the working RAK5802 sketch: UART0 GPIO0/1, DE GPIO6, and RE GPIO1.
constexpr uint8_t RS485_TX_PIN = 0;
constexpr uint8_t RS485_DE_PIN = WB_IO1;
constexpr uint8_t RS485_RE_PIN = 1;
constexpr uint32_t RS485_BAUD = 9600;
constexpr uint8_t Y4000_ADDRESS = 1;
constexpr uint8_t SEN0657_ADDRESS = 2;
constexpr size_t REQUEST_LENGTH = 8;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 10000;

uint16_t y4000Registers[16];
uint16_t sen0657Registers[14];
uint8_t requestFrame[REQUEST_LENGTH];
size_t requestLength = 0;
uint32_t lastHeartbeat = 0;

void printHexByte(uint8_t value)
{
    if (value < 0x10)
        Serial.print('0');
    Serial.print(value, HEX);
}

uint16_t modbusCrc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

void setY4000Float(uint8_t firstRegister, float value)
{
    uint8_t bytes[sizeof(value)];
    memcpy(bytes, &value, sizeof(value));
    y4000Registers[firstRegister] = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
    y4000Registers[firstRegister + 1] = (static_cast<uint16_t>(bytes[2]) << 8) | bytes[3];
}

bool lookupRegister(uint8_t address, uint16_t registerAddress, uint16_t &value)
{
    if (address == Y4000_ADDRESS && registerAddress >= 0x2601 && registerAddress <= 0x2610) {
        value = y4000Registers[registerAddress - 0x2601];
        return true;
    }

    if (address == SEN0657_ADDRESS && registerAddress >= 0x01F4 && registerAddress <= 0x0201) {
        if (registerAddress >= 0x01F4 && registerAddress <= 0x01F7)
            value = sen0657Registers[registerAddress - 0x01F4];
        else if (registerAddress >= 0x01F8 && registerAddress <= 0x01FA)
            value = sen0657Registers[4 + registerAddress - 0x01F8];
        else if (registerAddress >= 0x01FB && registerAddress <= 0x01FC)
            value = sen0657Registers[7 + registerAddress - 0x01FB];
        else if (registerAddress >= 0x01FD && registerAddress <= 0x0200)
            value = sen0657Registers[9 + registerAddress - 0x01FD];
        else if (registerAddress == 0x0201)
            value = sen0657Registers[13];
        else
            return false;
        return true;
    }

    return false;
}

void sendFrame(const uint8_t *data, size_t length)
{
    RS485.beginTransmission();
    RS485.write(data, length);
    RS485.endTransmission();

    Serial.print("TX");
    for (size_t i = 0; i < length; i++) {
        Serial.print(' ');
        printHexByte(data[i]);
    }
    Serial.println();
}

void sendException(uint8_t address, uint8_t function, uint8_t exceptionCode)
{
    uint8_t response[5] = {address, static_cast<uint8_t>(function | 0x80), exceptionCode, 0, 0};
    uint16_t crc = modbusCrc(response, 3);
    response[3] = static_cast<uint8_t>(crc);
    response[4] = static_cast<uint8_t>(crc >> 8);
    sendFrame(response, sizeof(response));
}

void handleReadRequest(uint8_t address, uint16_t startRegister, uint16_t count)
{
    if (count == 0 || count > 16) {
        sendException(address, 0x03, 0x03);
        return;
    }

    uint8_t response[37] = {address, 0x03, static_cast<uint8_t>(count * 2)};
    for (uint16_t i = 0; i < count; i++) {
        uint16_t value;
        if (!lookupRegister(address, startRegister + i, value)) {
            sendException(address, 0x03, 0x02);
            return;
        }
        response[3 + i * 2] = static_cast<uint8_t>(value >> 8);
        response[4 + i * 2] = static_cast<uint8_t>(value);
    }

    const size_t responseLength = 3 + count * 2;
    uint16_t crc = modbusCrc(response, responseLength);
    response[responseLength] = static_cast<uint8_t>(crc);
    response[responseLength + 1] = static_cast<uint8_t>(crc >> 8);
    sendFrame(response, responseLength + 2);
}

void handleRequest(const uint8_t *request)
{
    const uint16_t startRegister = (static_cast<uint16_t>(request[2]) << 8) | request[3];
    const uint16_t count = (static_cast<uint16_t>(request[4]) << 8) | request[5];
    uint16_t receivedCrc = static_cast<uint16_t>(request[6]) | (static_cast<uint16_t>(request[7]) << 8);
    const bool crcValid = receivedCrc == modbusCrc(request, 6);
    Serial.print("RX id=");
    Serial.print(request[0]);
    Serial.print(" fn=0x");
    printHexByte(request[1]);
    Serial.print(" reg=0x");
    Serial.print(startRegister, HEX);
    Serial.print(" count=");
    Serial.print(count);
    Serial.print(" crc=");
    Serial.println(crcValid ? "ok" : "BAD");
    if (!crcValid)
        return;

    const uint8_t address = request[0];
    const uint8_t function = request[1];
    if (address != Y4000_ADDRESS && address != SEN0657_ADDRESS)
        return;
    if (function != 0x03)
        return;

    handleReadRequest(address, startRegister, count);
}

void processRequestByte(uint8_t byte)
{
    requestFrame[requestLength++] = byte;
    if (requestLength < REQUEST_LENGTH)
        return;

    const bool knownAddress = requestFrame[0] == Y4000_ADDRESS || requestFrame[0] == SEN0657_ADDRESS;
    const uint16_t receivedCrc = static_cast<uint16_t>(requestFrame[6]) | (static_cast<uint16_t>(requestFrame[7]) << 8);
    if (knownAddress && requestFrame[1] == 0x03 && receivedCrc == modbusCrc(requestFrame, 6)) {
        handleRequest(requestFrame);
        requestLength = 0;
        return;
    }

    memmove(requestFrame, requestFrame + 1, REQUEST_LENGTH - 1);
    requestLength = REQUEST_LENGTH - 1;
}

void initializeSamples()
{
    // Y4000 registers 0x2601-0x2610 contain eight little-endian IEEE-754 values.
    setY4000Float(0, 8.25f);   // Dissolved oxygen, mg/L
    setY4000Float(2, 12.3f);   // Turbidity, NTU
    setY4000Float(4, 1.234f);  // Conductivity, mS/cm
    setY4000Float(6, 7.12f);   // pH
    setY4000Float(8, 20.5f);   // Temperature, C
    setY4000Float(10, 156.0f); // ORP, mV
    setY4000Float(12, 0.0f);   // Chlorophyll
    setY4000Float(14, 0.0f);   // Blue-green algae

    // SEN0657 documented registers, using its configured 9600-baud bus rate.
    sen0657Registers[0] = 23;    // 0x01F4 wind speed: 2.3 m/s
    sen0657Registers[1] = 0;
    sen0657Registers[2] = 2;     // 0x01F6 direction sector
    sen0657Registers[3] = 90;    // 0x01F7 direction degrees
    sen0657Registers[4] = 602;   // 0x01F8 humidity: 60.2%
    sen0657Registers[5] = 215;   // 0x01F9 temperature: 21.5 C
    sen0657Registers[6] = 0;
    sen0657Registers[7] = 0;
    sen0657Registers[8] = 0;
    sen0657Registers[9] = 1013;  // 0x01FD pressure: 101.3 kPa / 1013 hPa
    sen0657Registers[10] = 0;    // 0x01FE illumination high word
    sen0657Registers[11] = 5000; // 0x01FF illumination low word
    sen0657Registers[12] = 5000; // 0x0200 illumination
    sen0657Registers[13] = 0;    // 0x0201 rainfall: 0.0 mm
}

void setup()
{
    Serial.begin(115200);
    RS485.setPins(RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN);
    RS485.begin(RS485_BAUD);
    pinMode(WB_IO2, OUTPUT);
    digitalWrite(WB_IO2, HIGH);
    delay(500);
    RS485.receive();
    initializeSamples();
    Serial.println("RS485 sensor simulator ready: Y4000 ID 1, SEN0657 ID 2, 9600 baud");
    Serial.println("RS485 UART: GPIO0/1, DE=GPIO6, RE=GPIO1");
    lastHeartbeat = millis();
}

void loop()
{
    while (RS485.available()) {
        const uint8_t byte = static_cast<uint8_t>(RS485.read());
        Serial.print("RX byte=0x");
        printHexByte(byte);
        Serial.print(" index=");
        Serial.println(requestLength);
        processRequestByte(byte);
    }

    if (static_cast<uint32_t>(millis() - lastHeartbeat) >= HEARTBEAT_INTERVAL_MS) {
        Serial.println("RS485 RX-PROBE-20260929: listening on GPIO0/1, DE=6, RE=1");
        lastHeartbeat = millis();
    }
}