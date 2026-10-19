#pragma once

#include <Arduino.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#define DS18B20_PIN WB_IO3
#define DS18B20_SENSOR_COUNT 14

class DriverDS18B20 {
  public:
    static void init();
    static float readOneTemperature(size_t idx, bool controlPower = false);
    static bool readAllTemperatures(float* temps, bool controlPower = false);

  private:
    static OneWire oneWire;
    static DallasTemperature sensors;
    static const DeviceAddress ECE455_DS18B20_ADDRESSES[];
};