"""Fallback if the ChirpStack device profile codec cannot be edited.
Decodes the base64 'data' field of a ChirpStack uplink event (20 or 26 bytes) into the same
fields as chirpstack_decoder.js. Use in the dashboard: obj = decode_payload(event["data"])."""
import base64, struct

def decode_payload(b64):
    b = base64.b64decode(b64)
    if len(b) not in (20, 26):
        raise ValueError(f"expected 20 or 26 bytes, got {len(b)}")
    lat, lon, alt, temp, hum, press, vbat, sats, flags = struct.unpack_from("<iiHhHHHBB", b, 0)
    fix = bool(flags & 1)
    d = {
        "gps_active": bool(flags & 16), "gps_fix": fix, "satellites": sats, "altitude_m": alt,
        "battery_v": vbat / 1000.0, "battery_present": vbat > 0, "battery_pct": None,
        "temperature_c": None if temp == -32768 else temp / 100.0,
        "humidity_pct": None if hum == 0xFFFF else hum / 100.0,
        "pressure_hpa": None if press == 0xFFFF else press / 10.0,
        "bme_ok": bool(flags & 2), "sd_ok": bool(flags & 4),
    }
    if len(b) == 26:
        co, no2, nh3 = struct.unpack_from("<HHH", b, 20)
        d.update(gas_present=bool(flags & 32), gas_warm=bool(flags & 64),
                 gas_co_mv=None if co == 0xFFFF else co, gas_no2_mv=None if no2 == 0xFFFF else no2,
                 gas_nh3_mv=None if nh3 == 0xFFFF else nh3)
    if fix:
        d["latitude"], d["longitude"] = lat / 1e6, lon / 1e6
    return d
