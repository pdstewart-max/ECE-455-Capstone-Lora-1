import sqlite3
import json
import importlib.util
import math
import os
import struct
import subprocess
import sys
import threading


def ensure_dependencies():
    dependencies = {
        "pubsub": "PyPubSub",
        "meshtastic": "meshtastic",
        "flask": "Flask",
    }
    missing = [
        package for module, package in dependencies.items()
        if importlib.util.find_spec(module) is None
    ]
    if missing:
        print(f"Installing missing dependencies: {', '.join(missing)}")
        subprocess.check_call([sys.executable, "-m", "pip", "install", *missing])


ensure_dependencies()

from pubsub import pub
import meshtastic
import meshtastic.serial_interface
from flask import Flask, jsonify, render_template_string, redirect, request, url_for

# -------------------------------------------------------------------
# CONFIGURATION
# -------------------------------------------------------------------
SERIAL_PORT = os.environ.get("MESHTASTIC_SERIAL_PORT")
DB_FILE = os.environ.get("WATER_MONITOR_DB", "water_monitor.db")

METRIC_FIELDS = [
    ("battery_level", "INTEGER", "Device battery", "%"),
    ("voltage", "REAL", "Device voltage", "V"),
    ("channel_utilization", "REAL", "Channel utilization", "%"),
    ("air_util_tx", "REAL", "Transmit airtime utilization", "%"),
    ("uptime_seconds", "INTEGER", "Device uptime", "s"),
    ("temperature", "REAL", "Sensor temperature", "°C"),
    ("air_temp", "REAL", "Air temperature", "°C"),
    ("water_temp", "REAL", "Water temperature", "°C"),
    *[
        (f"water_temp_{index}", "REAL", f"Water temperature {index}", "°C")
        for index in range(14)
    ],
    ("relative_humidity", "REAL", "Relative humidity", "%"),
    ("barometric_pressure", "REAL", "Barometric pressure", "hPa"),
    ("gas_resistance", "REAL", "Gas resistance", "MΩ"),
    ("environment_voltage", "REAL", "Sensor voltage", "V"),
    ("current", "REAL", "Current", "A"),
    ("iaq", "INTEGER", "Indoor air quality", ""),
    ("distance", "REAL", "Distance", "mm"),
    ("lux", "REAL", "Illuminance", "lux"),
    ("white_lux", "REAL", "White illuminance", "lux"),
    ("ir_lux", "REAL", "Infrared illuminance", "lux"),
    ("uv_lux", "REAL", "Ultraviolet illuminance", "lux"),
    ("wind_direction", "INTEGER", "Wind direction", "°"),
    ("wind_speed", "REAL", "Wind speed", "m/s"),
    ("weight", "REAL", "Weight", "kg"),
    ("wind_gust", "REAL", "Wind gust", "m/s"),
    ("wind_lull", "REAL", "Wind lull", "m/s"),
    ("radiation", "REAL", "Radiation", "µR/h"),
    ("rainfall_1h", "REAL", "Rainfall (1 hour)", "mm"),
    ("rainfall_24h", "REAL", "Rainfall (24 hours)", "mm"),
    ("soil_moisture", "INTEGER", "Soil moisture", "%"),
    ("soil_temperature", "REAL", "Soil temperature", "°C"),
    ("one_wire_temperature", "TEXT", "One-wire temperatures", "°C"),
    ("adc_voltage_ch0", "REAL", "ADC voltage channel 0", "V"),
    ("adc_voltage_ch1", "REAL", "ADC voltage channel 1", "V"),
    ("adc_voltage_ch2", "REAL", "ADC voltage channel 2", "V"),
    ("adc_voltage_ch3", "REAL", "ADC voltage channel 3", "V"),
    ("adc_voltage_ch4", "REAL", "ADC voltage channel 4", "V"),
    ("adc_voltage_ch5", "REAL", "ADC voltage channel 5", "V"),
    ("adc_voltage_ch6", "REAL", "ADC voltage channel 6", "V"),
    ("adc_voltage_ch7", "REAL", "ADC voltage channel 7", "V"),
    ("one_wire_temperature_ch0", "REAL", "One-wire temperature channel 0", "°C"),
    ("one_wire_temperature_ch1", "REAL", "One-wire temperature channel 1", "°C"),
    ("one_wire_temperature_ch2", "REAL", "One-wire temperature channel 2", "°C"),
    ("one_wire_temperature_ch3", "REAL", "One-wire temperature channel 3", "°C"),
    ("one_wire_temperature_ch4", "REAL", "One-wire temperature channel 4", "°C"),
    ("one_wire_temperature_ch5", "REAL", "One-wire temperature channel 5", "°C"),
    ("one_wire_temperature_ch6", "REAL", "One-wire temperature channel 6", "°C"),
    ("one_wire_temperature_ch7", "REAL", "One-wire temperature channel 7", "°C"),
    ("lightning_strike_count_1h", "INTEGER", "Lightning strikes (1 hour)", ""),
    ("lightning_distance_km", "REAL", "Lightning distance", "km"),
    ("ch1_voltage", "REAL", "Power channel 1 voltage", "V"),
    ("ch1_current", "REAL", "Power channel 1 current", "A"),
    ("ch2_voltage", "REAL", "Power channel 2 voltage", "V"),
    ("ch2_current", "REAL", "Power channel 2 current", "A"),
    ("ch3_voltage", "REAL", "Power channel 3 voltage", "V"),
    ("ch3_current", "REAL", "Power channel 3 current", "A"),
    ("ch4_voltage", "REAL", "Power channel 4 voltage", "V"),
    ("ch4_current", "REAL", "Power channel 4 current", "A"),
    ("ch5_voltage", "REAL", "Power channel 5 voltage", "V"),
    ("ch5_current", "REAL", "Power channel 5 current", "A"),
    ("ch6_voltage", "REAL", "Power channel 6 voltage", "V"),
    ("ch6_current", "REAL", "Power channel 6 current", "A"),
    ("ch7_voltage", "REAL", "Power channel 7 voltage", "V"),
    ("ch7_current", "REAL", "Power channel 7 current", "A"),
    ("ch8_voltage", "REAL", "Power channel 8 voltage", "V"),
    ("ch8_current", "REAL", "Power channel 8 current", "A"),
    ("pm10_standard", "INTEGER", "Standard PM1.0", "µg/m³"),
    ("pm25_standard", "INTEGER", "Standard PM2.5", "µg/m³"),
    ("pm100_standard", "INTEGER", "Standard PM10", "µg/m³"),
    ("pm10_environmental", "INTEGER", "Environmental PM1.0", "µg/m³"),
    ("pm25_environmental", "INTEGER", "Environmental PM2.5", "µg/m³"),
    ("pm100_environmental", "INTEGER", "Environmental PM10", "µg/m³"),
    ("particles_03um", "INTEGER", "0.3 µm particle count", "#/0.1 L"),
    ("particles_05um", "INTEGER", "0.5 µm particle count", "#/0.1 L"),
    ("particles_10um", "INTEGER", "1.0 µm particle count", "#/0.1 L"),
    ("particles_25um", "INTEGER", "2.5 µm particle count", "#/0.1 L"),
    ("particles_50um", "INTEGER", "5.0 µm particle count", "#/0.1 L"),
    ("particles_100um", "INTEGER", "10.0 µm particle count", "#/0.1 L"),
    ("co2", "INTEGER", "CO₂ concentration", "ppm"),
    ("co2_temperature", "REAL", "CO₂ sensor temperature", "°C"),
    ("co2_humidity", "REAL", "CO₂ sensor humidity", "%"),
    ("form_formaldehyde", "REAL", "Formaldehyde", "ppb"),
    ("form_humidity", "REAL", "Formaldehyde sensor humidity", "%"),
    ("form_temperature", "REAL", "Formaldehyde sensor temperature", "°C"),
    ("pm40_standard", "INTEGER", "Standard PM4.0", "µg/m³"),
    ("particles_40um", "INTEGER", "4.0 µm particle count", "#/0.1 L"),
    ("pm_temperature", "REAL", "Particle sensor temperature", "°C"),
    ("pm_humidity", "REAL", "Particle sensor humidity", "%"),
    ("pm_voc_idx", "REAL", "Particle sensor VOC index", ""),
    ("pm_nox_idx", "REAL", "Particle sensor NOx index", ""),
    ("particles_tps", "REAL", "Typical particle size", "µm"),
    ("pm_status_flags", "INTEGER", "Particle sensor status flags", ""),
    ("soil_ph", "REAL", "Soil pH", ""),
    ("ph", "REAL", "Water pH", ""),
    ("electrical_conductivity", "REAL", "Electrical conductivity", "mS/cm"),
    ("salinity", "REAL", "Salinity", "mg/L"),
    ("nitrogen", "REAL", "Nitrogen", "mg/kg"),
    ("phosphorus", "REAL", "Phosphorus", "mg/kg"),
    ("potassium", "REAL", "Potassium", "mg/kg"),
    ("dissolved_oxygen", "REAL", "Dissolved oxygen", "mg/L"),
    ("orp", "REAL", "ORP", "mV"),
    ("chemical_oxygen_demand", "REAL", "Chemical oxygen demand", "mg/L"),
    ("turbidity", "REAL", "Turbidity", "NTU"),
    ("nitrate", "REAL", "Nitrate", "ppm"),
    ("ammonium", "REAL", "Ammonium", "ppm"),
    ("biochemical_oxygen_demand", "REAL", "Biochemical oxygen demand", "mg/L"),
    ("solar_irradiance", "REAL", "Solar irradiance", "W/m²"),
    ("chlorophyll", "REAL", "Chlorophyll", "µg/L"),
    ("bga", "REAL", "Blue-green algae (BGA)", "cells/mL"),
]
METRIC_COLUMNS = [field[0] for field in METRIC_FIELDS]
METRIC_LABELS = {
    column: {"column": column, "label": label, "unit": unit}
    for column, _, label, unit in METRIC_FIELDS
}
MAP_GROUPS = [
    {
        "label": "Device metrics",
        "metrics": [
            METRIC_LABELS[column]
            for column in ("battery_level", "voltage", "channel_utilization", "air_util_tx", "uptime_seconds")
        ],
    },
    {
        "label": "Water quality sonde",
        "metrics": [
            METRIC_LABELS[column]
            for column in (
                "water_temp",
                "electrical_conductivity", "dissolved_oxygen", "ph",
                "turbidity", "orp", "chlorophyll", "bga",
            )
        ],
    },
    {
        "label": "Temperature String",
        "metrics": [
            METRIC_LABELS[f"water_temp_{index}"]
            for index in range(14)
        ],
    },
    {
        "label": "Weather station",
        "metrics": [
            METRIC_LABELS[column]
            for column in (
                "air_temp", "relative_humidity", "barometric_pressure", "wind_speed",
                "wind_direction", "rainfall_1h", "lux",
            )
        ],
    },
]
MAP_METRICS = [metric for group in MAP_GROUPS for metric in group["metrics"]]
MAP_METRIC_COLUMNS = tuple(metric["column"] for metric in MAP_METRICS)

DEVICE_FIELDS = ("battery_level", "voltage", "channel_utilization", "air_util_tx", "uptime_seconds")
POWER_FIELDS = tuple(field for field in METRIC_COLUMNS if field.startswith("ch") and field.endswith(("_voltage", "_current")))
AIR_QUALITY_FIELDS = tuple(
    field for field in METRIC_COLUMNS
    if field.startswith(("pm", "particles_", "co2", "form_"))
)
POWER_AND_AIR_FIELDS = POWER_FIELDS + AIR_QUALITY_FIELDS
SOIL_WATER_FIELDS = (
    "soil_ph", "ph", "electrical_conductivity", "salinity", "nitrogen", "phosphorus",
    "potassium", "dissolved_oxygen", "orp", "chemical_oxygen_demand", "turbidity",
    "nitrate", "ammonium", "biochemical_oxygen_demand", "solar_irradiance", "chlorophyll", "bga",
)
ENVIRONMENT_FIELDS = tuple(
    field for field in METRIC_COLUMNS
    if field not in DEVICE_FIELDS
    and field not in POWER_AND_AIR_FIELDS
    and field not in SOIL_WATER_FIELDS
)

# -------------------------------------------------------------------
# HELPER FUNCTIONS
# -------------------------------------------------------------------
def camel_case(name):
    first, *rest = name.split("_")
    return first + "".join(part.title() for part in rest)


def get_variant(telemetry, name):
    if not isinstance(telemetry, dict):
        return {}
    value = telemetry.get(name) or telemetry.get(camel_case(name))
    return value if isinstance(value, dict) else {}


def get_field_values(source, fields, aliases=None):
    if not isinstance(source, dict):
        return {}
    aliases = aliases or {}
    values = {}
    for name in fields:
        value = source.get(name, source.get(camel_case(name), source.get(aliases.get(name))))
        if value is not None:
            values[name] = json.dumps(value) if isinstance(value, list) else value
    return values


def read_varint(data, offset):
    value = 0
    shift = 0
    while offset < len(data) and shift < 70:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, offset
        shift += 7
    raise ValueError("Malformed protobuf varint in telemetry payload")


def decode_soil_water_metrics(payload):
    """Decode Telemetry.soil_water_metrics when an older Python client treats it as unknown."""
    if not isinstance(payload, (bytes, bytearray)):
        return {}

    values = {}
    offset = 0
    while offset < len(payload):
        tag, offset = read_varint(payload, offset)
        field_number, wire_type = tag >> 3, tag & 7
        if wire_type == 0:
            _, offset = read_varint(payload, offset)
        elif wire_type == 1:
            offset += 8
        elif wire_type == 2:
            length, offset = read_varint(payload, offset)
            end = offset + length
            if end > len(payload):
                raise ValueError("Truncated length-delimited telemetry field")
            if field_number == 11:
                nested = payload[offset:end]
                nested_offset = 0
                while nested_offset < len(nested):
                    nested_tag, nested_offset = read_varint(nested, nested_offset)
                    nested_number, nested_wire = nested_tag >> 3, nested_tag & 7
                    if nested_wire == 5:
                        if nested_offset + 4 > len(nested):
                            raise ValueError("Truncated soil-water float field")
                        number = struct.unpack_from("<f", nested, nested_offset)[0]
                        # SoilWaterMetrics field numbers 1-15 follow this tuple's order.
                        if 1 <= nested_number <= len(SOIL_WATER_FIELDS) and math.isfinite(number):
                            values[SOIL_WATER_FIELDS[nested_number - 1]] = number
                        nested_offset += 4
                    elif nested_wire == 0:
                        _, nested_offset = read_varint(nested, nested_offset)
                    elif nested_wire == 1:
                        nested_offset += 8
                    elif nested_wire == 2:
                        nested_length, nested_offset = read_varint(nested, nested_offset)
                        nested_offset += nested_length
                    else:
                        raise ValueError("Unsupported wire type in soil-water telemetry")
                if nested_offset > len(nested):
                    raise ValueError("Truncated soil-water telemetry field")
            offset = end
        elif wire_type == 5:
            offset += 4
        else:
            raise ValueError("Unsupported wire type in telemetry payload")
        if offset > len(payload):
            raise ValueError("Truncated telemetry payload")
    return values


def json_safe(value):
    if isinstance(value, (bytes, bytearray)):
        return value.hex()
    if isinstance(value, dict):
        return {key: json_safe(item) for key, item in value.items() if key not in ("raw", "payload")}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    if hasattr(value, "ListFields"):
        return {field.name: json_safe(item) for field, item in value.ListFields()}
    return value

# -------------------------------------------------------------------
# 1. Database Setup
# -------------------------------------------------------------------
def init_db():
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS nodes (
            node_id TEXT PRIMARY KEY,
            node_name TEXT,
            lat REAL,
            lon REAL,
            last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            node_id TEXT,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY(node_id) REFERENCES nodes(node_id)
        )
    ''')
    node_columns = {row[1] for row in cursor.execute("PRAGMA table_info(nodes)")}
    if "node_name" not in node_columns:
        cursor.execute("ALTER TABLE nodes ADD COLUMN node_name TEXT")
    cursor.execute("""
        UPDATE nodes SET node_name = 'Node ' || upper(replace(node_id, '!', ''))
        WHERE node_name IS NULL OR node_name = ''
    """)
    existing_columns = {row[1] for row in cursor.execute("PRAGMA table_info(telemetry)")}
    for column, sql_type, _, _ in METRIC_FIELDS:
        if column not in existing_columns:
            cursor.execute(f"ALTER TABLE telemetry ADD COLUMN {column} {sql_type}")
    for old_column, new_column in (
        ("conductivity", "electrical_conductivity"),
        ("humidity", "relative_humidity"),
    ):
        if old_column in existing_columns and new_column not in existing_columns:
            cursor.execute(f"UPDATE telemetry SET {new_column} = {old_column}")
    cursor.execute(
        "CREATE INDEX IF NOT EXISTS telemetry_node_id_id_idx ON telemetry (node_id, id)"
    )
    conn.commit()
    conn.close()

init_db()

# -------------------------------------------------------------------
# 2. Meshtastic Serial Packet Listener
# -------------------------------------------------------------------
def sync_known_nodes(interface):
    node_cache = getattr(interface, "nodes", {}) or {}
    if not hasattr(node_cache, "items"):
        return

    conn = sqlite3.connect(DB_FILE)
    try:
        cursor = conn.cursor()
        for key, node in node_cache.items():
            if not isinstance(node, dict):
                continue
            user = node.get("user", {})
            user = user if isinstance(user, dict) else {}
            node_id = user.get("id") or (key if isinstance(key, str) and key.startswith("!") else None)
            node_num = node.get("num")
            if not node_id and isinstance(node_num, int):
                node_id = f"!{node_num:08x}"
            if not node_id:
                continue
            node_name = user.get("longName") or user.get("long_name") or f"Node {node_id.lstrip('!').upper()}"
            cursor.execute(
                "INSERT OR IGNORE INTO nodes (node_id, node_name, last_seen) VALUES (?, ?, NULL)",
                (node_id, node_name),
            )
        conn.commit()
    finally:
        conn.close()


def on_receive(packet, interface):
    conn = None
    try:
        from_id = packet.get("fromId")
        decoded = packet.get("decoded", {})
        portnum = decoded.get("portnum")

        if not from_id:
            return

        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        node_name = None
        packet_user = decoded.get("user") or packet.get("user") or {}
        if isinstance(packet_user, dict):
            node_name = packet_user.get("longName") or packet_user.get("long_name")
        if not node_name and interface is not None:
            node_cache = getattr(interface, "nodes", {}) or {}
            node_info = node_cache.get(from_id, {}) if hasattr(node_cache, "get") else {}
            if isinstance(node_info, dict):
                node_user = node_info.get("user", {})
                if isinstance(node_user, dict):
                    node_name = node_user.get("longName") or node_user.get("long_name")
        if not node_name:
            node_name = f"Node {from_id.lstrip('!').upper()}"
        cursor.execute(
            """
            INSERT INTO nodes (node_id, node_name, last_seen)
            VALUES (?, ?, CURRENT_TIMESTAMP)
            ON CONFLICT(node_id) DO UPDATE SET
                node_name=CASE
                    WHEN excluded.node_name NOT LIKE 'Node %' THEN excluded.node_name
                    ELSE nodes.node_name
                END,
                last_seen=CURRENT_TIMESTAMP
            """,
            (from_id, node_name),
        )

        # --- GPS POSITION HANDLER ---
        if portnum == "POSITION_APP":
            pos = decoded.get("position", {})
            lat = pos.get("latitude") or (pos.get("latitudeI") / 1e7 if pos.get("latitudeI") else None)
            lon = pos.get("longitude") or (pos.get("longitudeI") / 1e7 if pos.get("longitudeI") else None)
            
            # Reject invalid or unfixed GPS fixes (e.g. 0.026, 0.026 or 0, 0)
            if lat and lon and abs(lat) > 1.0 and abs(lon) > 1.0:
                cursor.execute('''
                    INSERT INTO nodes (node_id, node_name, lat, lon, last_seen)
                    VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
                    ON CONFLICT(node_id) DO UPDATE SET
                        lat=excluded.lat, lon=excluded.lon, last_seen=CURRENT_TIMESTAMP
                ''', (from_id, node_name, lat, lon))
                print(f"\n[GPS VALID] Node {from_id} -> Lat: {lat:.6f}, Lon: {lon:.6f}")
            else:
                print(f"\n[GPS REJECTED] Node {from_id} sent unfixed coords: ({lat}, {lon})")

        # --- TELEMETRY HANDLER ---
        elif portnum == "TELEMETRY_APP":
            telemetry = decoded.get("telemetry", {})
            
            print(f"\n==========================================")
            print(f"[RAW TELEMETRY DECODED] From Node {from_id}:")
            print(json.dumps(json_safe(decoded), indent=2))
            print(f"==========================================")

            device_metrics = get_variant(telemetry, "device_metrics")
            environment_metrics = get_variant(telemetry, "environment_metrics")
            soil_water_metrics = get_variant(telemetry, "soil_water_metrics")
            power_metrics = get_variant(telemetry, "power_metrics")
            air_quality_metrics = get_variant(telemetry, "air_quality_metrics")
            temp_string_metrics = get_variant(telemetry, "temp_string_metrics")
            values = get_field_values(device_metrics, DEVICE_FIELDS)
            values.update(get_field_values(
                environment_metrics, ENVIRONMENT_FIELDS, {"environment_voltage": "voltage"}
            ))
            values.update(get_field_values(soil_water_metrics, SOIL_WATER_FIELDS))
            values.update(get_field_values(power_metrics, POWER_FIELDS))
            values.update(get_field_values(air_quality_metrics, AIR_QUALITY_FIELDS))
            values.update(decode_soil_water_metrics(decoded.get("payload")))

            # Unpack the 28-byte custom temperature array into individual columns.
            if temp_string_metrics and "temperatures" in temp_string_metrics:
                raw_bytes = temp_string_metrics.get("temperatures")
                if raw_bytes:
                    for i in range(0, min(len(raw_bytes), 28), 2):
                        sensor_idx = i // 2
                        val_int = int.from_bytes(raw_bytes[i:i+2], byteorder='little', signed=True)
                        values[f"water_temp_{sensor_idx}"] = val_int / 16
                    print(f" -> [CUSTOM TEMP STRING DECODED] 14 Sensors parsed successfully.")

            temp = values.pop("temperature", None)
            if temp is not None:
                values["air_temp" if any(
                    values.get(field) is not None
                    for field in ("relative_humidity", "barometric_pressure", "wind_speed")
                ) else "water_temp"] = temp

            if values:
                cursor.execute(
                    "INSERT OR IGNORE INTO nodes (node_id, node_name) VALUES (?, ?)",
                    (from_id, node_name),
                )
                cursor.execute("INSERT INTO telemetry (node_id) VALUES (?)", (from_id,))
                row_id = cursor.lastrowid
                columns = list(values)
                assignments = ", ".join(f"{column} = ?" for column in columns)
                cursor.execute(
                    f"UPDATE telemetry SET {assignments} WHERE id = ?",
                    [values[column] for column in columns] + [row_id],
                )
                print(f" -> [SAVED TELEMETRY] Node {from_id} | {values}")

        conn.commit()

    except Exception as e:
        print(f"[Parser Error]: {e}")
    finally:
        if conn is not None:
            conn.close()

# Start USB Listener in background thread
def start_meshtastic():
    pub.subscribe(on_receive, "meshtastic.receive")
    try:
        port_description = SERIAL_PORT or "the auto-detected port"
        print(f"Connecting to USB Base Node on {port_description}...")
        serial_interface = meshtastic.serial_interface.SerialInterface(devPath=SERIAL_PORT)
        if not serial_interface.devPath:
            raise RuntimeError("No Meshtastic serial device was detected.")
        sync_known_nodes(serial_interface)
        print(f"Connected to USB Base Node on {serial_interface.devPath} successfully!")
    except SystemExit as e:
        print(f"USB Connection Error: {e}")
    except Exception as e:
        print(f"USB Connection Error on {port_description}: {e}")

# -------------------------------------------------------------------
# 3. Local Web Dashboard (Flask + Leaflet.js)
# -------------------------------------------------------------------
app = Flask(__name__)

HTML_UI = """
<!DOCTYPE html>
<html>
<head>
    <title>Water Quality Monitoring Map</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
    <style>
        body { margin: 0; font-family: Arial, sans-serif; color: #e2e8f0; background: #0b1120; }
        #map { position: fixed; inset: 0; }
        .leaflet-tile { filter: brightness(.72) invert(1) hue-rotate(180deg); }
        select, button { padding: 8px 10px; font-size: 14px; border: 1px solid #334155; border-radius: 5px; color: #e2e8f0; background: #1e293b; }
        button { cursor: pointer; }
        #node-status { position: fixed; top: 12px; right: 12px; z-index: 1000; width: min(320px, calc(100vw - 24px)); max-height: 45vh; overflow-y: auto; padding: 12px; border: 1px solid #334155; border-radius: 8px; background: #111827; box-shadow: 0 2px 10px #0008; }
        #node-status h2 { margin: 0 0 4px; font-size: 16px; }
        #node-status-count { margin-bottom: 8px; color: #94a3b8; font-size: 12px; }
        #node-status-list { display: grid; gap: 6px; }
        .node-status-item { display: block; padding: 7px 8px; border-radius: 5px; color: #e2e8f0; text-decoration: none; background: #1e293b; }
        .node-status-item:hover { background: #334155; }
        .node-status-name { font-weight: 700; }
        .node-status-detail { margin-top: 3px; color: #cbd5e1; font-size: 12px; }
        .node-label { color: #f8fafc; background: #111827; border: 1px solid #64748b; box-shadow: 0 1px 4px #0008; font-weight: 700; padding: 2px 5px; }
        .leaflet-tooltip-top.node-label::before { border-top-color: #111827; }
        .leaflet-popup-content-wrapper, .leaflet-popup-tip { color: #e2e8f0; background: #111827; }
        .leaflet-popup-content a { color: #7dd3fc; }
        .leaflet-bar, .leaflet-bar a, .leaflet-bar a:hover { color: #e2e8f0; border-color: #334155; background: #1e293b; }
        .leaflet-control-attribution { color: #cbd5e1 !important; background: #111827d9 !important; }
        .leaflet-control-attribution a { color: #7dd3fc !important; }
        .leaflet-control-reset button { border: 0; color: white; background: #dc2626; font-weight: 700; cursor: pointer; }
        .leaflet-control-reset button:hover { background: #b91c1c; }
    </style>
</head>
<body>

<div id="map"></div>
<section id="node-status" aria-live="polite">
    <h2>Nodes known to base</h2>
    <div id="node-status-count">Waiting for USB node list…</div>
    <div id="node-status-list"></div>
</section>

<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
    const map = L.map('map').setView([43.0696, -89.4162], 14);

    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        maxZoom: 19,
        attribution: '© OpenStreetMap'
    }).addTo(map);

    let markers = new Map();
    let nodeData = [];

    async function updateData() {
        try {
            const res = await fetch('/api/nodes');
            if (!res.ok) throw new Error(`Node request failed: ${res.status}`);
            nodeData = await res.json();
            renderNodeStatus();
            renderMap();
        } catch (e) {
            console.error("Error updating map data:", e);
        }
    }

    function getNodeColor(nodeId) {
        let hash = 2166136261;
        for (const character of nodeId) {
            hash ^= character.charCodeAt(0);
            hash = Math.imul(hash, 16777619);
        }
        return `hsl(${(hash >>> 0) % 360}, 68%, 58%)`;
    }

    function escapeHtml(value) {
        return String(value).replace(/[&<>"']/g, character => ({
            '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
        })[character]);
    }

    function lastSeenText(value) {
        if (!value) return 'no packet received by app yet';
        const parsed = new Date(value.replace(' ', 'T') + 'Z');
        return Number.isNaN(parsed.getTime()) ? 'packet received; time unavailable' : `last heard ${parsed.toLocaleTimeString()}`;
    }

    function renderNodeStatus() {
        const located = nodeData.filter(node => node.has_position);
        const unlocated = nodeData.length - located.length;
        document.getElementById('node-status-count').textContent =
            `${nodeData.length} known · ${located.length} with GPS · ${unlocated} without GPS`;
        document.getElementById('node-status-list').innerHTML = nodeData.map(node => {
            const label = node.node_name || node.node_id;
            const details = [
                node.has_position ? 'GPS position available' : 'No GPS position',
                lastSeenText(node.last_seen),
                node.battery_level !== null && node.battery_level !== undefined ? `Battery ${node.battery_level}%` : null,
                node.voltage !== null && node.voltage !== undefined ? `${Number(node.voltage).toPrecision(5)} V` : null,
            ].filter(Boolean).join(' · ');
            return `<a class="node-status-item" href="/node-data?node=${encodeURIComponent(node.node_id)}"><div class="node-status-name" style="color:${getNodeColor(node.node_id)}">${escapeHtml(label)}</div><div class="node-status-detail">${escapeHtml(node.node_id)} · ${escapeHtml(details)}</div></a>`;
        }).join('') || '<div class="node-status-detail">No nodes heard yet. The base node will appear after a packet arrives.</div>';
    }

    const resetControl = L.control({ position: 'bottomleft' });
    resetControl.onAdd = function() {
        const container = L.DomUtil.create('div', 'leaflet-bar leaflet-control leaflet-control-reset');
        const button = L.DomUtil.create('button', '', container);
        button.type = 'button';
        button.textContent = 'Clear database';
        button.title = 'Clear database and remove ghost nodes';
        L.DomEvent.disableClickPropagation(container);
        L.DomEvent.on(button, 'click', async () => {
            if (!confirm('Clear all saved node locations and telemetry history?')) return;
            try {
                const response = await fetch('/reset', { method: 'POST' });
                if (!response.ok) throw new Error(`Database reset failed: ${response.status}`);
                await updateData();
            } catch (error) {
                console.error('Could not clear database:', error);
                alert('Could not clear the database. See the browser console for details.');
            }
        });
        return container;
    };
    resetControl.addTo(map);

    const dashboardControl = L.control({ position: 'topleft' });
    dashboardControl.onAdd = function() {
        const container = L.DomUtil.create('div', 'leaflet-bar');
        const link = L.DomUtil.create('a', '', container);
        link.href = '/node-data';
        link.title = 'Open node data dashboard';
        link.setAttribute('aria-label', 'Open node data dashboard');
        link.textContent = 'Data';
        link.style.cssText = 'width:auto;padding:0 10px;text-decoration:none;line-height:30px;font-weight:700';
        L.DomEvent.disableClickPropagation(container);
        return container;
    };
    dashboardControl.addTo(map);

    function renderMap() {
        const visibleNodes = new Set();
        nodeData.forEach(node => {
            if (!node.has_position || node.lat === null || node.lat === undefined ||
                node.lon === null || node.lon === undefined) return;
            visibleNodes.add(node.node_id);
            let marker = markers.get(node.node_id);
            if (!marker) {
                marker = L.circleMarker([node.lat, node.lon], {
                    radius: 9,
                    fillColor: getNodeColor(node.node_id),
                    color: '#ffffff',
                    weight: 2,
                    fillOpacity: 0.95,
                }).addTo(map);
                marker.bindTooltip(node.node_name || node.node_id, {
                    permanent: true,
                    direction: 'top',
                    offset: [0, -7],
                    className: 'node-label',
                });
                marker.bindPopup(`<b>${escapeHtml(node.node_name || node.node_id)}</b><br/><a href="/node-data?node=${encodeURIComponent(node.node_id)}">View node data</a>`);
                marker.on('click', () => {
                    window.location.href = `/node-data?node=${encodeURIComponent(node.node_id)}`;
                });
                markers.set(node.node_id, marker);
            }
            marker.setLatLng([node.lat, node.lon]);
        });
        for (const [nodeId, marker] of markers) {
            if (!visibleNodes.has(nodeId)) {
                map.removeLayer(marker);
                markers.delete(nodeId);
            }
        }
    }

    updateData();
    setInterval(updateData, 5000);
</script>
</body>
</html>
"""

NODE_DATA_UI = """
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Node data dashboard</title>
    <style>
        * { box-sizing: border-box; }
        html { color-scheme: dark; }
        body { margin: 0; background: #0b1120; color: #e2e8f0; font-family: Arial, sans-serif; }
        header { position: sticky; top: 0; z-index: 2; display: flex; align-items: center; gap: 18px; flex-wrap: wrap; padding: 14px 22px; background: #020617; color: #f8fafc; border-bottom: 1px solid #1e293b; }
        header a { color: white; text-decoration: none; font-weight: 700; }
        header h1 { margin: 0; font-size: 21px; }
        header .node-picker { display: flex; align-items: center; gap: 8px; margin-left: auto; }
        main { max-width: 1500px; margin: 0 auto; padding: 22px; }
        select, input, button { min-height: 38px; border: 1px solid #334155; border-radius: 5px; color: #e2e8f0; background: #1e293b; padding: 8px 10px; font-size: 14px; }
        button { cursor: pointer; }
        button:hover { background: #334155; }
        .panel { margin-bottom: 18px; padding: 18px; border: 1px solid #263449; border-radius: 9px; background: #111827; box-shadow: 0 1px 3px #0005; }
        .panel h2 { margin: 0 0 14px; font-size: 18px; }
        .muted { color: #94a3b8; font-size: 13px; }
        #node-location { margin: -7px 0 15px; }
        .metric-group { margin-top: 17px; }
        .metric-group h3 { margin: 0 0 9px; color: #cbd5e1; font-size: 14px; }
        .metric-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 9px; }
        .metric-card { min-width: 0; padding: 10px; border: 1px solid #334155; border-radius: 6px; background: #0f172a; }
        .metric-card-label { color: #94a3b8; font-size: 12px; }
        .metric-card-value { margin-top: 5px; font-weight: 700; overflow-wrap: anywhere; }
        .chart-controls { display: grid; grid-template-columns: repeat(auto-fit, minmax(165px, 1fr)); align-items: end; gap: 12px; }
        .control { display: flex; flex-direction: column; gap: 5px; }
        .control label { font-size: 12px; color: #cbd5e1; font-weight: 700; }
        .scale-buttons { display: flex; gap: 7px; align-self: end; }
        .scale-buttons button { flex: 1; }
        .chart-panel { padding: 16px; }
        .chart-panel h2 { margin-bottom: 4px; }
        .chart-canvas { display: block; width: 100%; height: 440px; margin-top: 12px; }
        #additional-nodes { display: flex; flex-wrap: wrap; gap: 8px 16px; margin-top: 12px; }
        .node-check { display: inline-flex; align-items: center; gap: 6px; font-size: 13px; }
        .node-check input { min-height: 0; }
        .status { min-height: 18px; margin-top: 8px; }
        @media (max-width: 750px) {
            main { padding: 12px; }
            header { padding: 12px; }
            header .node-picker { width: 100%; margin-left: 0; }
            header .node-picker select { flex: 1; }
            .chart-controls { grid-template-columns: 1fr 1fr; }
            .chart-controls .control:first-child { grid-column: 1 / -1; }
            .scale-buttons { grid-column: 1 / -1; }
            .chart-canvas { height: 330px; }
        }
    </style>
</head>
<body>
<header>
    <a href="/" aria-label="Back to map">← Map</a>
    <h1 id="page-title">Node data</h1>
    <div class="node-picker">
        <label for="node-select">View node</label>
        <select id="node-select"></select>
    </div>
</header>
<main>
    <section class="panel">
        <h2>Current readings</h2>
        <div id="node-location" class="muted"></div>
        <div id="current-readings"></div>
    </section>

    <section class="panel chart-panel">
        <h2>Measurement history</h2>
        <div class="chart-controls">
            <div class="control">
                <label for="metric-select">Measurement</label>
                <select id="metric-select">
                    {% for group in map_groups %}
                    <optgroup label="{{ group.label }}">
                        {% for metric in group.metrics %}
                        <option value="{{ metric.column }}"{% if metric.column == 'air_temp' %} selected{% endif %}>{{ metric.label }}{% if metric.unit %} ({{ metric.unit }}){% endif %}</option>
                        {% endfor %}
                    </optgroup>
                    {% endfor %}
                </select>
            </div>
            <div class="control" id="temperature-unit-control">
                <label for="temperature-unit">Temperature</label>
                <select id="temperature-unit">
                    <option value="F" selected>Fahrenheit (°F)</option>
                    <option value="C">Celsius (°C)</option>
                </select>
            </div>
            <div class="control">
                <label for="scale-min">Vertical axis minimum (<span id="scale-min-unit">°F</span>)</label>
                <input id="scale-min" type="number" step="any" placeholder="Auto">
            </div>
            <div class="control">
                <label for="scale-max">Vertical axis maximum (<span id="scale-max-unit">°F</span>)</label>
                <input id="scale-max" type="number" step="any" placeholder="Auto">
            </div>
            <div class="control">
                <label for="time-min">Horizontal axis start</label>
                <input id="time-min" type="datetime-local">
            </div>
            <div class="control">
                <label for="time-max">Horizontal axis end</label>
                <input id="time-max" type="datetime-local">
            </div>
            <div class="scale-buttons">
                <button id="apply-scale" type="button">Apply scale</button>
                <button id="auto-scale" type="button">Auto axes</button>
            </div>
        </div>
        <div id="scale-status" class="status muted"></div>
        <h3 id="chart-title">Selected node history</h3>
        <div class="muted">The selected node is always shown. Select other nodes to compare their history.</div>
        <div id="additional-nodes"></div>
        <canvas id="history-chart" class="chart-canvas" aria-label="Measurement history across selected nodes"></canvas>
        <div id="history-chart-status" class="status muted"></div>
    </section>
</main>
<script>
    const metricGroups = {{ map_groups|tojson }};
    const metrics = metricGroups.flatMap(group => group.metrics);
    const initialNodeId = {{ initial_node_id|tojson }};
    const nodeSelect = document.getElementById('node-select');
    const metricSelect = document.getElementById('metric-select');
    const unitSelect = document.getElementById('temperature-unit');
    const scaleMin = document.getElementById('scale-min');
    const scaleMax = document.getElementById('scale-max');
    const timeMinInput = document.getElementById('time-min');
    const timeMaxInput = document.getElementById('time-max');
    const temperatureMetrics = new Set(['air_temp', 'water_temp']);
    const colors = ['#2563eb', '#dc2626', '#059669', '#9333ea', '#ea580c', '#0891b2', '#be123c', '#4d7c0f'];
    let nodes = [];
    let requestSequence = 0;
    let cachedSeries = [];
    let previousTemperatureUnit = unitSelect.value;

    function escapeHtml(value) {
        return String(value).replace(/[&<>"']/g, character => ({
            '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
        })[character]);
    }

    function formatValue(value) {
        const number = Number(value);
        return Number.isFinite(number) ? String(Number(number.toPrecision(5))) : String(value);
    }

    function isTemperature(metric) {
        return temperatureMetrics.has(metric);
    }

    function displayValue(value, metric) {
        const number = Number(value);
        if (isTemperature(metric) && unitSelect.value === 'F') return formatValue(number * 9 / 5 + 32);
        return formatValue(number);
    }

    function displayUnit(metric) {
        if (isTemperature(metric)) return unitSelect.value === 'F' ? '°F' : '°C';
        return metrics.find(item => item.column === metric).unit;
    }

    function colorForNode(nodeId) {
        let hash = 2166136261;
        for (const character of nodeId) {
            hash ^= character.charCodeAt(0);
            hash = Math.imul(hash, 16777619);
        }
        return `hsl(${(hash >>> 0) % 360}, 68%, 62%)`;
    }

    function selectedNode() {
        return nodes.find(node => node.node_id === nodeSelect.value);
    }

    function populateNodes() {
        const previous = nodeSelect.value;
        nodeSelect.innerHTML = nodes.map(node =>
            `<option value="${escapeHtml(node.node_id)}">${escapeHtml(node.node_name || node.node_id)} (${escapeHtml(node.node_id)})${node.has_position ? '' : ' · no GPS'}</option>`
        ).join('');
        const requested = nodes.find(node => node.node_id === initialNodeId);
        const retained = nodes.find(node => node.node_id === previous);
        nodeSelect.value = (retained || requested || nodes[0] || {}).node_id || '';
        renderCurrentReadings();
        renderComparisonChoices();
    }

    function renderCurrentReadings() {
        const node = selectedNode();
        if (!node) {
            document.getElementById('page-title').textContent = 'No nodes available';
            document.getElementById('node-location').textContent = 'No nodes are known to the USB base node yet.';
            document.getElementById('current-readings').innerHTML = '';
            return;
        }
        document.getElementById('page-title').textContent = node.node_name || node.node_id;
        const locationText = node.has_position
            ? `${Number(node.lat).toFixed(5)}, ${Number(node.lon).toFixed(5)}`
            : 'No GPS position reported';
        document.getElementById('node-location').textContent =
            `${node.node_id} · ${locationText} · Last heard by app: ${node.last_seen || 'no packet yet'} · Last telemetry: ${node.timestamp || 'none'}`;
        document.getElementById('current-readings').innerHTML = metricGroups.map(group => {
            const cards = group.metrics
                .filter(metric => node[metric.column] !== null && node[metric.column] !== undefined)
                .map(metric => `<div class="metric-card"><div class="metric-card-label">${escapeHtml(metric.label)}</div><div class="metric-card-value">${escapeHtml(displayValue(node[metric.column], metric.column))}${displayUnit(metric.column) ? ` ${escapeHtml(displayUnit(metric.column))}` : ''}</div></div>`)
                .join('');
            return cards ? `<section class="metric-group"><h3>${escapeHtml(group.label)}</h3><div class="metric-grid">${cards}</div></section>` : '';
        }).join('') || '<p class="muted">No telemetry received yet.</p>';
    }

    function renderComparisonChoices() {
        const currentId = nodeSelect.value;
        const selected = new Set(Array.from(document.querySelectorAll('#additional-nodes input:checked')).map(input => input.value));
        const container = document.getElementById('additional-nodes');
        container.innerHTML = nodes.filter(node => node.node_id !== currentId).map(node => {
            const label = node.node_name || node.node_id;
            return `<label class="node-check"><input type="checkbox" value="${escapeHtml(node.node_id)}"${selected.has(node.node_id) ? ' checked' : ''}><span style="color:${colorForNode(node.node_id)}">●</span>${escapeHtml(label)}</label>`;
        }).join('') || '<span class="muted">No other heard nodes are available to compare.</span>';
    }

    function selectedComparisonIds() {
        return Array.from(document.querySelectorAll('#additional-nodes input:checked')).map(input => input.value);
    }

    async function refreshNodes() {
        try {
            const response = await fetch('/api/nodes');
            if (!response.ok) throw new Error(`Node request failed: ${response.status}`);
            nodes = await response.json();
            populateNodes();
            await loadCharts();
        } catch (error) {
            console.error('Could not refresh nodes:', error);
        }
    }

    async function fetchSeries(nodeId, metric) {
        const response = await fetch(`/api/nodes/${encodeURIComponent(nodeId)}/history/${encodeURIComponent(metric)}`);
        if (!response.ok) throw new Error(`History request failed: ${response.status}`);
        const points = await response.json();
        return {
            nodeId,
            label: (nodes.find(node => node.node_id === nodeId) || {}).node_name || nodeId,
            color: colorForNode(nodeId),
            points: points.map(point => ({
                x: new Date(point.timestamp.replace(' ', 'T') + 'Z').getTime(),
                y: isTemperature(metric) && unitSelect.value === 'F' ? Number(point.value) * 9 / 5 + 32 : Number(point.value),
            })).filter(point => Number.isFinite(point.x) && Number.isFinite(point.y)),
        };
    }

    function drawChart(canvasId, statusId, series, metric, allowManualScale) {
        const canvas = document.getElementById(canvasId);
        const bounds = canvas.getBoundingClientRect();
        const ratio = window.devicePixelRatio || 1;
        const width = Math.max(1, bounds.width);
        const height = Math.max(260, bounds.height);
        canvas.width = Math.floor(width * ratio);
        canvas.height = Math.floor(height * ratio);
        const context = canvas.getContext('2d');
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, width, height);

        const allPoints = series.flatMap(item => item.points);
        if (!allPoints.length) {
            document.getElementById(statusId).textContent = 'No saved readings for this measurement yet.';
            return;
        }

        let timeMin = timeMinInput.value ? new Date(timeMinInput.value).getTime() : Math.min(...allPoints.map(point => point.x));
        let timeMax = timeMaxInput.value ? new Date(timeMaxInput.value).getTime() : Math.max(...allPoints.map(point => point.x));
        if (!timeMinInput.value && !timeMaxInput.value && timeMin === timeMax) timeMax = timeMin + 1;
        if (!Number.isFinite(timeMin) || !Number.isFinite(timeMax) || timeMin >= timeMax) {
            document.getElementById(statusId).textContent = 'Horizontal axis start must be earlier than its end.';
            return;
        }

        const visibleSeries = series.map(item => ({
            ...item,
            points: item.points.filter(point => point.x >= timeMin && point.x <= timeMax),
        }));
        const points = visibleSeries.flatMap(item => item.points);
        if (!points.length) {
            document.getElementById(statusId).textContent = 'No saved readings in the selected time range.';
            return;
        }

        let min = Math.min(...points.map(point => point.y));
        let max = Math.max(...points.map(point => point.y));
        const hasManualMin = allowManualScale && scaleMin.value.trim() !== '';
        const hasManualMax = allowManualScale && scaleMax.value.trim() !== '';
        if (hasManualMin) min = Number(scaleMin.value);
        if (hasManualMax) max = Number(scaleMax.value);
        if (min === max && !hasManualMin && !hasManualMax) {
            const margin = Math.abs(min) * 0.05 || 1;
            min -= margin;
            max += margin;
        }
        if (!Number.isFinite(min) || !Number.isFinite(max) || min >= max) {
            document.getElementById(statusId).textContent = 'Scale minimum must be less than the scale maximum.';
            return;
        }

        if (timeMin === timeMax) timeMax = timeMin + 1;
        const pad = { left: 72, right: 20, top: 22, bottom: 52 };
        const plotWidth = width - pad.left - pad.right;
        const plotHeight = height - pad.top - pad.bottom;
        context.font = '12px Arial';
        context.fillStyle = '#94a3b8';
        context.strokeStyle = '#334155';
        context.lineWidth = 1;

        for (let tick = 0; tick <= 5; tick++) {
            const y = pad.top + plotHeight * tick / 5;
            const value = max - (max - min) * tick / 5;
            context.beginPath();
            context.moveTo(pad.left, y);
            context.lineTo(width - pad.right, y);
            context.stroke();
            context.textAlign = 'right';
            context.fillText(formatValue(value), pad.left - 9, y + 4);
        }

        const dateTicks = width < 520 ? 2 : width < 800 ? 3 : 4;
        const tickFormat = timeMax - timeMin <= 24 * 60 * 60 * 1000
            ? { hour: '2-digit', minute: '2-digit' }
            : { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' };
        for (let tick = 0; tick <= dateTicks; tick++) {
            const x = pad.left + plotWidth * tick / dateTicks;
            const time = new Date(timeMin + (timeMax - timeMin) * tick / dateTicks);
            context.beginPath();
            context.moveTo(x, pad.top);
            context.lineTo(x, height - pad.bottom);
            context.stroke();
            context.textAlign = tick === 0 ? 'left' : tick === dateTicks ? 'right' : 'center';
            context.fillText(time.toLocaleString([], tickFormat), x, height - pad.bottom + 19);
        }

        visibleSeries.forEach(item => {
            if (!item.points.length) return;
            const sorted = item.points.slice().sort((a, b) => a.x - b.x);
            context.strokeStyle = item.color;
            context.fillStyle = item.color;
            context.lineWidth = 2;
            context.beginPath();
            sorted.forEach((point, index) => {
                const x = pad.left + plotWidth * (point.x - timeMin) / (timeMax - timeMin);
                const y = pad.top + plotHeight * (max - point.y) / (max - min);
                if (index === 0) context.moveTo(x, y);
                else context.lineTo(x, y);
            });
            context.stroke();
            sorted.forEach(point => {
                const x = pad.left + plotWidth * (point.x - timeMin) / (timeMax - timeMin);
                const y = pad.top + plotHeight * (max - point.y) / (max - min);
                context.beginPath();
                context.arc(x, y, 3, 0, Math.PI * 2);
                context.fill();
            });
        });

        let legendX = pad.left;
        let legendY = height - 10;
        context.font = '12px Arial';
        context.textAlign = 'left';
        series.filter(item => item.points.length).forEach(item => {
            context.fillStyle = item.color;
            context.fillRect(legendX, legendY - 9, 12, 3);
            context.fillStyle = '#cbd5e1';
            const legendLabel = context.measureText(item.label).width;
            context.fillText(item.label, legendX + 17, legendY - 5);
            legendX += legendLabel + 34;
            if (legendX > width - 120) {
                legendX = pad.left;
                legendY -= 17;
            }
        });

        const rangeDescription = `${new Date(timeMin).toLocaleString()} – ${new Date(timeMax).toLocaleString()}`;
        document.getElementById(statusId).textContent =
            `${points.length} readings · ${rangeDescription} · vertical range ${formatValue(min)} to ${formatValue(max)} ${displayUnit(metric)}`;
    }

    function renderCharts() {
        const metric = metricSelect.value;
        const selected = selectedNode();
        const metricInfo = metrics.find(item => item.column === metric);
        const unit = displayUnit(metric);
        document.getElementById('temperature-unit-control').style.visibility = isTemperature(metric) ? 'visible' : 'hidden';
        document.getElementById('chart-title').textContent =
            `${selected ? selected.node_name || selected.node_id : 'Selected node'} · ${metricInfo.label} history`;
        document.getElementById('scale-min').setAttribute('aria-label', `Vertical axis minimum in ${unit}`);
        document.getElementById('scale-max').setAttribute('aria-label', `Vertical axis maximum in ${unit}`);
        document.getElementById('scale-min-unit').textContent = unit || '—';
        document.getElementById('scale-max-unit').textContent = unit || '—';
        drawChart('history-chart', 'history-chart-status', cachedSeries, metric, true);
    }

    async function loadCharts() {
        const sequence = ++requestSequence;
        const nodeIds = [nodeSelect.value, ...selectedComparisonIds()].filter(Boolean);
        if (!nodeIds.length) {
            cachedSeries = [];
            renderCharts();
            return;
        }
        document.getElementById('history-chart-status').textContent = 'Loading history…';
        try {
            const result = await Promise.all(nodeIds.map(nodeId => fetchSeries(nodeId, metricSelect.value)));
            if (sequence !== requestSequence) return;
            cachedSeries = result;
            renderCharts();
        } catch (error) {
            if (sequence !== requestSequence) return;
            console.error('Could not load chart data:', error);
            document.getElementById('history-chart-status').textContent = 'Could not load measurement history.';
        }
    }

    nodeSelect.addEventListener('change', () => {
        const url = new URL(window.location.href);
        url.searchParams.set('node', nodeSelect.value);
        window.history.replaceState({}, '', url);
        renderCurrentReadings();
        renderComparisonChoices();
        loadCharts();
    });
    metricSelect.addEventListener('change', loadCharts);
    unitSelect.addEventListener('change', () => {
        if (previousTemperatureUnit !== unitSelect.value && isTemperature(metricSelect.value)) {
            [scaleMin, scaleMax].forEach(input => {
                if (input.value.trim() === '') return;
                const value = Number(input.value);
                input.value = formatValue(unitSelect.value === 'F' ? value * 9 / 5 + 32 : (value - 32) * 5 / 9);
            });
        }
        previousTemperatureUnit = unitSelect.value;
        renderCurrentReadings();
        loadCharts();
    });
    document.getElementById('additional-nodes').addEventListener('change', loadCharts);
    document.getElementById('apply-scale').addEventListener('click', renderCharts);
    document.getElementById('auto-scale').addEventListener('click', () => {
        scaleMin.value = '';
        scaleMax.value = '';
        timeMinInput.value = '';
        timeMaxInput.value = '';
        document.getElementById('scale-status').textContent = 'Automatic horizontal and vertical ranges enabled.';
        renderCharts();
    });
    [scaleMin, scaleMax, timeMinInput, timeMaxInput].forEach(input => input.addEventListener('change', renderCharts));
    window.addEventListener('resize', renderCharts);
    refreshNodes();
    setInterval(refreshNodes, 30000);
</script>
</body>
</html>
"""

@app.route('/')
def home():
    return render_template_string(HTML_UI, map_groups=MAP_GROUPS)


@app.route('/node-data')
def node_data_page():
    return render_template_string(
        NODE_DATA_UI,
        map_groups=MAP_GROUPS,
        initial_node_id=request.args.get("node", ""),
    )

@app.route('/api/nodes')
def get_nodes():
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    
    # Query valid coordinates (excluding 0,0 and un-fixed coordinates)
    metric_columns = ", ".join(
        f"(SELECT t.{column} FROM telemetry t WHERE t.node_id = n.node_id "
        f"AND t.{column} IS NOT NULL ORDER BY t.id DESC LIMIT 1) AS {column}"
        for column in MAP_METRIC_COLUMNS
    )
    cursor.execute(f"""
        SELECT n.node_id, n.node_name, n.lat, n.lon, n.last_seen,
           CASE WHEN n.lat IS NOT NULL AND n.lon IS NOT NULL
                AND abs(n.lat) > 1.0 AND abs(n.lon) > 1.0
                THEN 1 ELSE 0 END AS has_position,
           (SELECT MAX(t.timestamp) FROM telemetry t WHERE t.node_id = n.node_id) AS timestamp,
           {metric_columns}
    FROM nodes n
    ORDER BY n.last_seen DESC
    """)
    
    rows = cursor.fetchall()
    conn.close()
    return jsonify([dict(r) for r in rows])


@app.route('/api/nodes/<path:node_id>/history/<metric>')
def get_node_history(node_id, metric):
    if metric not in MAP_METRIC_COLUMNS:
        return jsonify({"error": "Unknown metric"}), 400

    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute(
        f"SELECT timestamp, {metric} AS value FROM telemetry "
        "WHERE node_id = ? AND " + metric + " IS NOT NULL ORDER BY id",
        (node_id,),
    )
    history = [dict(row) for row in cursor.fetchall()]
    conn.close()
    return jsonify(history)

@app.route('/reset', methods=['POST'])
def reset_db():
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute('DELETE FROM telemetry;')
    cursor.execute('DELETE FROM nodes;')
    conn.commit()
    conn.close()
    print("\n[DATABASE RESET] All ghost nodes and historical records purged.")
    return redirect(url_for('home'))

if __name__ == '__main__':
    threading.Thread(target=start_meshtastic, daemon=True).start()
    app.run(host='127.0.0.1', port=5000)