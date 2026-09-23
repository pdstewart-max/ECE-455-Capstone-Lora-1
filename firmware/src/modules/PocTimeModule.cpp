#include "PocTimeModule.h"
#include "GPSStatus.h"
#include "gps/RTC.h"
#include "mesh/NodeDB.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <time.h>

PocTimeModule *pocTimeModule;

PocTimeModule::PocTimeModule()
    : SinglePortModule("PocTime", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("PocTime")
{
    setInterval(60 * 1000);
}

int32_t PocTimeModule::runOnce()
{
    char timeStr[32] = "RTC Syncing...";
    const uint32_t nowSecs = getTime(true);

    if (nowSecs != 0) {
        const time_t now = static_cast<time_t>(nowSecs);
        struct tm timeinfo;
        if (localtime_r(&now, &timeinfo)) {
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        }
    }

    char gpsStr[128] = "GPS: unavailable";
    if (gpsStatus != nullptr) {
        if (gpsStatus->getIsConnected()) {
            const double lat = gpsStatus->getLatitude() * 1e-7;
            const double lon = gpsStatus->getLongitude() * 1e-7;
            snprintf(gpsStr, sizeof(gpsStr), "GPS: %s lat=%.6f lon=%.6f alt=%d sats=%u",
                     gpsStatus->getHasLock() ? "LOCK" : "NOLOCK", lat, lon, gpsStatus->getAltitude(),
                     gpsStatus->getNumSatellites());
        } else {
            snprintf(gpsStr, sizeof(gpsStr), "GPS: disconnected");
        }
    }

    char envStr[192] = "ENV: unavailable";
#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
    if (nodeDB != nullptr) {
        meshtastic_EnvironmentMetrics env = meshtastic_EnvironmentMetrics_init_zero;
        if (nodeDB->copyNodeEnvironment(nodeDB->getNodeNum(), env)) {
            snprintf(envStr, sizeof(envStr), "ENV: temp=%.2fC rh=%.2f%% baro=%.2fkPa lux=%.0f",
                     env.has_temperature ? env.temperature : NAN, env.has_relative_humidity ? env.relative_humidity : NAN,
                     env.has_barometric_pressure ? env.barometric_pressure : NAN,
                     env.has_lux ? env.lux : NAN);
        }
    }
#endif

    LOG_INFO("PocTime: RTC=%s | %s | %s", timeStr, gpsStr, envStr);
    return 60 * 1000;
}