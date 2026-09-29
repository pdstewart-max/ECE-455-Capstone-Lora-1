#pragma once

#include <Arduino.h>

#include "concurrency/OSThread.h"
#include "mesh/SinglePortModule.h"

class RS485TelemetryModule : public SinglePortModule, public concurrency::OSThread {
public:
	RS485TelemetryModule();

protected:
	int32_t runOnce() override;

private:
	uint16_t calculateCRC16(const uint8_t *buffer, uint16_t length);
	bool sendModbusQuery(uint8_t slaveId, uint8_t funcCode, uint16_t startReg, uint16_t regCount, uint8_t *responseBuf,
						 size_t &responseLen);
	float bytesToFloat(const uint8_t *bytes);
	void readYosemitechSonde();
	void readDFRobotWeather();
};

extern RS485TelemetryModule *rs485TelemetryModule;
