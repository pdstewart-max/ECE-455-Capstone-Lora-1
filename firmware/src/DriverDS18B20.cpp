#include "configuration.h"

#if defined(RAK11310)

#include "DriverDS18B20.h"

const DeviceAddress DriverDS18B20::ECE455_DS18B20_ADDRESSES[] = {
    {0x28, 0x32, 0x2B, 0xFD, 0x0A, 0x00, 0x00, 0x93}, // 0 - bottom / end
    {0x28, 0x28, 0xDF, 0xBD, 0x0B, 0x00, 0x00, 0xF5}, // 1
    {0x28, 0x42, 0x47, 0xBD, 0x0B, 0x00, 0x00, 0x70}, // 2
    {0x28, 0x13, 0xD9, 0xFC, 0x0A, 0x00, 0x00, 0xB8}, // 3
    {0x28, 0xB3, 0x50, 0xBD, 0x0B, 0x00, 0x00, 0x18}, // 4
    {0x28, 0x60, 0xA7, 0xBE, 0x0B, 0x00, 0x00, 0xFA}, // 5
    {0x28, 0x1A, 0x61, 0xBD, 0x0B, 0x00, 0x00, 0x9B}, // 6
    {0x28, 0xB5, 0x54, 0xBD, 0x0B, 0x00, 0x00, 0xB5}, // 7
    {0x28, 0x17, 0x68, 0xBD, 0x0B, 0x00, 0x00, 0x22}, // 8
    {0x28, 0x79, 0x6E, 0xFC, 0x0A, 0x00, 0x00, 0xAA}, // 9
    {0x28, 0xD9, 0x69, 0xBD, 0x0B, 0x00, 0x00, 0x63}, // 10
    {0x28, 0x01, 0x68, 0xBD, 0x0B, 0x00, 0x00, 0xCB}, // 11
    {0x28, 0xC4, 0x57, 0xBB, 0x0B, 0x00, 0x00, 0x5D}, // 12
    {0x28, 0x02, 0x6D, 0xBD, 0x0B, 0x00, 0x00, 0x40}, // 13 - top / front
};

OneWire DriverDS18B20::oneWire(DS18B20_PIN);
DallasTemperature DriverDS18B20::sensors(&oneWire);

void DriverDS18B20::init() {
    sensors.begin();
}

float DriverDS18B20::readOneTemperature(size_t idx, bool controlPower) {
    if (controlPower) {
        // 1. Enable switched 3.3V power rail (3V3_S)
        pinMode(WB_IO2, OUTPUT);
        digitalWrite(WB_IO2, HIGH);
        // 2. Allow sensor time to stabilize after power-up
        delay(50);
    }

    // 3. request conversion, and read temperature
    sensors.requestTemperatures();
    float tempC = sensors.getTempC(ECE455_DS18B20_ADDRESSES[idx]);

    // 4. Cut power to 3V3_S to minimize idle current drain
    if (controlPower) digitalWrite(WB_IO2, LOW);

    // 5. Validate reading (DallasTemperature returns -127.0 on error)
    if (tempC == DEVICE_DISCONNECTED_C) return -127.0f;

    return tempC;
}

// Always returns true right now
bool DriverDS18B20::readAllTemperatures(float* temps, bool controlPower) {
    if (controlPower) {
        // 1. Enable switched 3.3V power rail (3V3_S)
        pinMode(WB_IO2, OUTPUT);
        digitalWrite(WB_IO2, HIGH);
        // 2. Allow sensor time to stabilize after power-up
        delay(50);
    }

    sensors.requestTemperatures();
    for (size_t i = 0; i < DS18B20_SENSOR_COUNT; i++) {
        delay(40);
        float tempC = sensors.getTempC(ECE455_DS18B20_ADDRESSES[i]);
        LOG_INFO("DS18B20 %zu Temperature: %.2f C", i, tempC);

        // 5. Validate reading (DallasTemperature returns -127.0 on error)
        if (tempC == DEVICE_DISCONNECTED_C) LOG_WARN("DS18B20 %zu Temperature reading failed (disconnected)", i);
        temps[i] = tempC;
    }

    // 4. Cut power to 3V3_S to minimize idle current drain
    if (controlPower) digitalWrite(WB_IO2, LOW);

    return true;
}

#endif