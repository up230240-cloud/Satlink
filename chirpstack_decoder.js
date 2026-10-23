// ChirpStack v4 payload codec for the Satlink station (26-byte uplink; 20-byte older firmware also accepted).
//
// HOW TO USE: ChirpStack -> Device profiles -> (the profile of your device) -> "Codec" tab ->
// Payload codec "JavaScript functions" -> paste this whole file -> Submit.
// Decoded values then appear in the device "Events" tab (field "object").
//
// The field names below are the ones the PICARO dashboard (exercises 10/11) reads:
//   gps_active, gps_fix, latitude, longitude, altitude_m, satellites,
//   battery_v, battery_pct, charging, usb_powered, temperature_c, pressure_hpa
// Extra fields it does not show yet (but stores in its raw_json): humidity_pct, alt_baro_m, ...
//
// Payload layout (little-endian) - keep it in sync with struct Telemetry in src/main.cpp:
//   [0..3]   lat    int32   degrees * 1e6
//   [4..7]   lon    int32   degrees * 1e6
//   [8..9]   alt    uint16  GPS altitude, metres
//   [10..11] temp   int16   degC * 100        (-32768 = sensor missing)
//   [12..13] hum    uint16  %RH * 100         (65535  = sensor missing)
//   [14..15] press  uint16  hPa * 10          (65535  = sensor missing)
//   [16..17] vbat   uint16  millivolts        (0 = no battery connected)
//   [18]     sats   uint8
//   [19]     flags  uint8   bit0 GPS fix, bit1 BME280 ok, bit2 SD ok, bit3 joined, bit4 GPS sending data,
//                           bit5 gas sensor present, bit6 gas warmed up
//   [20..21] co     uint16  MICS-6814 CO  output, raw mV   (65535 = none)   (26-byte frames only)
//   [22..23] no2    uint16  MICS-6814 NO2 output, raw mV
//   [24..25] nh3    uint16  MICS-6814 NH3 output, raw mV

var SEA_LEVEL_HPA = 1013.25;   // change to the launch-day sea-level pressure for a better barometric altitude

function baroAltitude(pHpa) {
  // ISA standard atmosphere (valid up to 20 km)
  if (pHpa >= 226.32) return 44330.77 * (1 - Math.pow(pHpa / SEA_LEVEL_HPA, 0.190263));
  return 11000 - 6341.62 * Math.log(pHpa / 226.32);
}

function decodeUplink(input) {
  var b = input.bytes;
  var warnings = [];
  var errors = [];
  if (b.length !== 20 && b.length !== 26) {
    errors.push("expected 20 or 26 bytes, got " + b.length);
    return { data: {}, warnings: warnings, errors: errors };
  }
  var v = new DataView(new Uint8Array(b).buffer);
  var temp = v.getInt16(10, true);
  var hum = v.getUint16(12, true);
  var press = v.getUint16(14, true);
  var vbat = v.getUint16(16, true);
  var flags = v.getUint8(19);
  var fix = (flags & 1) !== 0;

  var data = {
    gps_active: (flags & 16) !== 0,
    gps_fix: fix,
    satellites: v.getUint8(18),
    altitude_m: v.getUint16(8, true),
    battery_v: vbat / 1000.0,
    battery_present: vbat > 0,
    battery_pct: null,                 // not measured: the dashboard shows "--"
    temperature_c: temp === -32768 ? null : temp / 100.0,
    humidity_pct: hum === 0xFFFF ? null : hum / 100.0,
    pressure_hpa: press === 0xFFFF ? null : press / 10.0,
    bme_ok: (flags & 2) !== 0,
    sd_ok: (flags & 4) !== 0
  };
  if (b.length === 26) {
    var co = v.getUint16(20, true), no2 = v.getUint16(22, true), nh3 = v.getUint16(24, true);
    data.gas_present = (flags & 32) !== 0;
    data.gas_warm = (flags & 64) !== 0;
    data.gas_co_mv = co === 0xFFFF ? null : co;
    data.gas_no2_mv = no2 === 0xFFFF ? null : no2;
    data.gas_nh3_mv = nh3 === 0xFFFF ? null : nh3;
  }
  data.alt_baro_m = data.pressure_hpa === null ? null : Math.round(baroAltitude(data.pressure_hpa));

  if (fix) {
    data.latitude = v.getInt32(0, true) / 1e6;
    data.longitude = v.getInt32(4, true) / 1e6;
  } else {
    warnings.push("No GPS fix: latitude/longitude not included");
  }
  return { data: data, warnings: warnings, errors: errors };
}

// ChirpStack expects this function to exist. Commands (beacon, challenge-response) will be added later.
function encodeDownlink(input) {
  return { bytes: [], fPort: input.fPort || 1, warnings: [], errors: [] };
}
