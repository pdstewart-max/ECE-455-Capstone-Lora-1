#pragma once

#include <Arduino.h>

#include "concurrency/OSThread.h"
#include "mesh/ProtobufModule.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

#if defined(RAK11310)
class RS485TelemetryModule : private concurrency::OSThread, public ProtobufModule<meshtastic_Telemetry> {
	public:
		RS485TelemetryModule();

	protected:
		bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *telemetry) override;
		int32_t runOnce() override;

	private:
		bool sensorPowerOn = false;
		bool sensorReadComplete = false;
		uint32_t sensorPowerStartedAtMs = 0;
		bool readRegisters(uint8_t slaveId, uint16_t startRegister, uint16_t count, uint16_t *registers);
		// bool writeRegister(uint8_t slaveId, uint16_t registerAddr, uint16_t value);
		bool readY4000(meshtastic_SoilWaterMetrics &waterMetrics, float &temperature);
		bool readSEN0657(meshtastic_EnvironmentMetrics &weatherMetrics, uint16_t &rainfallTenthsMm);
		bool sendTelemetry(const meshtastic_Telemetry &telemetry);
		static uint16_t calculateCRC16(const uint8_t *buffer, size_t length);
		static float decodeY4000Float(const uint16_t *registers);
};

extern RS485TelemetryModule *rs485TelemetryModule;
#endif
