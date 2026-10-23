// Satlink 2026 - minimal flight computer for the LilyGO T-Beam Supreme v3.0
//
// Every cycle it:
//   1. reads GPS (position, altitude), BME280 (temp, humidity, pressure) and battery voltage
//   2. prints one CSV line on Serial (USB-C)
//   3. appends the same line to the microSD, if a card is present (works fine without one)
//   4. sends a 26-byte LoRaWAN uplink (class A, OTAA, US915 sub-band 2) once the node has joined
//
// Change the cycle by editing cycleSeconds below, or at run time by typing a number
// (seconds, minimum 5) in the serial monitor and pressing Enter.

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <RadioLib.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <TinyGPSPlus.h>
#include <Adafruit_BME280.h>
#include "keys.h"
#include "settings.h"
#include "util.h"
#include "gas.h"
#include "display.h"

uint32_t cycleSeconds = CYCLE_SECONDS_DEFAULT;   // change CYCLE_SECONDS_DEFAULT in settings.h

// ---- T-Beam Supreme pin map (from LilyGO utilities.h) ----
#define I2C_SDA          17   // Wire : BME280, QMC6310 magnetometer, OLED
#define I2C_SCL          18
#define I2C1_SDA         42   // Wire1: AXP2101 PMU, RTC
#define I2C1_SCL         41
#define GPS_RX_PIN        9   // ESP32 RX  <- GPS TX
#define GPS_TX_PIN        8   // ESP32 TX  -> GPS RX
#define GPS_BAUD       9600
#define RADIO_SCLK_PIN   12   // SX1262 on the default SPI bus
#define RADIO_MISO_PIN   13
#define RADIO_MOSI_PIN   11
#define RADIO_CS_PIN     10
#define RADIO_RST_PIN     5
#define RADIO_DIO1_PIN    1
#define RADIO_BUSY_PIN    4
#define SD_SCLK_PIN      36   // microSD on its own SPI bus
#define SD_MISO_PIN      37
#define SD_MOSI_PIN      35
#define SD_CS_PIN        47

// ---- Objects ----
XPowersPMU pmu;
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
LoRaWANNode node(&radio, &US915, 2);   // US915, sub-band 2 (channels 8-15 + 65), class A by default
TinyGPSPlus gps;
Adafruit_BME280 bme;
SPIClass sdSPI(HSPI);
File logFile;
Preferences prefs;

bool pmuOk = false, bmeOk = false, sdOk = false, radioOk = false, joined = false;
ScreenData scr = {};
bool echoNmea = false;   // toggled by typing 'g' + Enter in the serial monitor
enum GpsModel { GPS_UNKNOWN, GPS_L76K, GPS_UBLOX };
GpsModel gpsModel = GPS_UNKNOWN;

#define BOOT_BTN_PIN      0   // BOOT button: next screen page

// LoRaWAN identifiers, parsed from keys.h at start-up
uint64_t joinEui = 0, devEui = 0;
uint8_t appKey[16] = {0}, nwkKey[16] = {0};
bool keysOk = false;

// 26-byte payload, little-endian (decoder: chirpstack_decoder.js)
struct __attribute__((packed)) Telemetry {
    int32_t  lat_e6;      // degrees * 1e6
    int32_t  lon_e6;      // degrees * 1e6
    uint16_t alt_m;       // GPS altitude, metres (clamped at 0)
    int16_t  temp_cC;     // 0.01 degC      (INT16_MIN  = no sensor)
    uint16_t hum_cPct;    // 0.01 %RH       (0xFFFF     = no sensor)
    uint16_t press_dhPa;  // 0.1 hPa        (0xFFFF     = no sensor)
    uint16_t vbat_mV;     // battery voltage, 0 if no battery
    uint8_t  sats;
    uint8_t  flags;       // bit0 GPS fix, bit1 BME280 ok, bit2 SD ok, bit3 joined, bit4 GPS sending data, bit5 gas sensor present, bit6 gas warmed up
    uint16_t co_mV;       // raw module outputs in mV (0xFFFF = none)
    uint16_t no2_mV;
    uint16_t nh3_mV;
};
static_assert(sizeof(Telemetry) == 26, "payload layout changed");

// ============================ GPS: detect + configure ============================
static void sendNmea(const char *body) {          // body without '$' and '*checksum'
    uint8_t cs = 0;
    for (const char *p = body; *p; ++p) cs ^= (uint8_t)*p;
    char line[96];
    snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
    Serial1.write((const uint8_t *)line, strlen(line));
}

static void sendUbx(uint8_t cls, uint8_t id, const uint8_t *payload, uint16_t len) {
    uint8_t head[4] = {cls, id, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
    uint8_t a = 0, b = 0;
    for (uint8_t v : head) { a += v; b += a; }
    for (uint16_t i = 0; i < len; i++) { a += payload[i]; b += a; }
    Serial1.write((uint8_t)0xB5);
    Serial1.write((uint8_t)0x62);
    Serial1.write(head, 4);
    if (len) Serial1.write(payload, len);
    Serial1.write(a);
    Serial1.write(b);
}

static int findBytes(const uint8_t *buf, size_t n, const char *pat, size_t plen) {
    for (size_t i = 0; i + plen <= n; i++)
        if (memcmp(buf + i, pat, plen) == 0) return (int)i;
    return -1;
}

// Ask the module who it is: u-blox answers the UBX-MON-VER poll, the Quectel L76K answers $PCAS06.
static GpsModel detectGps() {
    static uint8_t buf[2048];
    size_t n = 0;
    while (Serial1.available()) Serial1.read();
    sendUbx(0x0A, 0x04, nullptr, 0);   // UBX-MON-VER poll
    sendNmea("PCAS06,0");              // L76K version request
    for (uint32_t t0 = millis(); millis() - t0 < 2500;) {
        while (Serial1.available() && n < sizeof(buf)) buf[n++] = Serial1.read();
        delay(5);
    }

    int u = findBytes(buf, n, "\xB5\x62\x0A\x04", 4);
    if (u >= 0) {
        // MON-VER extensions contain e.g. "MOD=MAX-M10S"
        int m = findBytes(buf, n, "MOD=", 4);
        char model[24] = "";
        if (m >= 0) {
            size_t k = 0;
            for (size_t i = m + 4; i < n && buf[i] >= 0x20 && k < sizeof(model) - 1; i++) model[k++] = buf[i];
            model[k] = 0;
        }
        Serial.printf("GPS         : u-blox %s\n", model);
        return GPS_UBLOX;
    }
    int l = findBytes(buf, n, "$GPTXT,01,01,02", 15);
    if (l >= 0) {
        Serial.print("GPS         : Quectel L76K (reply: ");
        for (size_t i = l; i < n && buf[i] != '\r' && buf[i] != '\n' && i < (size_t)l + 70; i++) Serial.write(buf[i]);
        Serial.println(")");
        return GPS_L76K;
    }
    Serial.printf("GPS         : model not recognised (%u bytes received)\n", (unsigned)n);
    return GPS_UNKNOWN;
}

// Airborne dynamic model: without it, consumer receivers can lose the fix or clip the
// altitude at high altitude / during balloon ascent.
static void configureGps(GpsModel m) {
    // If the module was not recognised we send both: the wrong one is simply ignored.
#if GPS_L76K_AIRBORNE
    if (m == GPS_L76K || m == GPS_UNKNOWN) {
        sendNmea("PCAS11,5");   // L76K: airborne < 1 g (mode number still to be confirmed in documentation)
        delay(100);
    }
#endif
    if (m == GPS_UBLOX || m == GPS_UNKNOWN) {
        // UBX-CFG-VALSET, RAM layer:
        //   CFG-NAVSPG-DYNMODEL (0x20110021) = 6  (airborne < 1 g)
        //   CFG-RATE-MEAS       (0x30210001) = 1000 ms (1 Hz, easy on a 9600 baud link)
        const uint8_t valset[] = {0x00, 0x01, 0x00, 0x00,
                                  0x21, 0x00, 0x11, 0x20, 0x06,
                                  0x01, 0x00, 0x21, 0x30, 0xE8, 0x03};
        sendUbx(0x06, 0x8A, valset, sizeof(valset));
    }
}

// ============================ Hardware setup ============================
static bool setupPower() {
    if (!pmu.begin(Wire1, AXP2101_SLAVE_ADDRESS, I2C1_SDA, I2C1_SCL)) return false;
    // DC1 powers the ESP32
    pmu.setALDO4Voltage(3300); pmu.enableALDO4();   // GPS
    pmu.setALDO3Voltage(3300); pmu.enableALDO3();   // LoRa radio

    // Power-cycle the sensor and SD rails so nothing is left holding a bus after a reset.
    pmu.disableALDO1(); pmu.disableALDO2(); pmu.disableBLDO1();
    delay(250);
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();   // BME280 + OLED + magnetometer
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();   // sensors
    pmu.setBLDO1Voltage(3300); pmu.enableBLDO1();   // microSD
    pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();   // 2x13 expansion header 3.3 V

    pmu.enableBattVoltageMeasure();
    return true;
}

static bool setupBme() {
    Wire.begin(I2C_SDA, I2C_SCL);
    return bme.begin(0x77, &Wire) || bme.begin(0x76, &Wire);
}

// Returns false (quietly) when there is no card: the station keeps working without it.
static bool setupSd() {
    sdSPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, sdSPI, 4000000)) return false;
    bool isNew = !SD.exists(LOG_FILE);
    logFile = SD.open(LOG_FILE, FILE_APPEND);
    if (!logFile) return false;
    if (isNew) {
        logFile.println("ms,utc,fix,sats,lat,lon,alt_gps_m,temp_c,hum_pct,press_hpa,vbat_mv,joined,lora_state,co_mv,no2_mv,nh3_mv");
        logFile.flush();
    }
    return true;
}

// ============================ LoRaWAN ============================
// Reads keys.h and says exactly what is missing. Returns true when we can try to join.
static bool loadKeys() {
    bool ok = true;
    if (!parseHexU64(LORAWAN_DEVEUI, &devEui)) { Serial.println("[keys.h] DEVEUI must be 16 hex characters"); ok = false; }
    if (!parseHexU64(LORAWAN_JOINEUI, &joinEui)) { Serial.println("[keys.h] JOINEUI must be 16 hex characters"); ok = false; }
    int n = hexToBytes(LORAWAN_APPKEY, appKey, 16);
    if (n == 0) { Serial.println("[keys.h] APPKEY is empty -> paste the 32-character AppKey (not joining yet)"); ok = false; }
    else if (n != 32) { Serial.printf("[keys.h] APPKEY has %d hex characters, it needs exactly 32\n", n); ok = false; }
#if LORAWAN_USE_1_1
    n = hexToBytes(LORAWAN_NWKKEY, nwkKey, 16);
    if (n != 32) { Serial.println("[keys.h] NWKKEY must be 32 hex characters when LORAWAN_USE_1_1 is 1"); ok = false; }
#endif
    return ok;
}

static void saveNonces() {   // the network rejects a join that reuses an old DevNonce, so keep it across resets
    prefs.putBytes("nonces", node.getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
}

static bool setupLoRaWAN() {
    SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
    int st = radio.begin(903.9);   // frequency is overridden by LoRaWAN
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoRa] radio init failed, code %d\n", st);
        return false;
    }
    keysOk = loadKeys();
    if (!keysOk) {
        Serial.println("[LoRaWAN] keys.h incomplete: not joining (Serial + SD logging only)");
        return true;
    }
#if LORAWAN_USE_1_1
    st = node.beginOTAA(joinEui, devEui, nwkKey, appKey);
#else
    st = node.beginOTAA(joinEui, devEui, NULL, appKey);   // NULL nwkKey = LoRaWAN 1.0.x
#endif
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoRaWAN] beginOTAA failed, code %d\n", st);
        return false;
    }
    prefs.begin("lorawan", false);
    if (prefs.getBytesLength("nonces") == RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
        uint8_t buf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
        prefs.getBytes("nonces", buf, sizeof(buf));
        node.setBufferNonces(buf);
    }
    return true;
}

static void tryJoin() {
    Serial.println("[LoRaWAN] joining...");
    int16_t st = node.activateOTAA();
    saveNonces();
    if (st == RADIOLIB_LORAWAN_NEW_SESSION || st == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        node.setADR(false);               // the capsule moves: keep a fixed data rate
        node.setDatarate(LORAWAN_DR);
        joined = true;
        Serial.println("[LoRaWAN] joined!");
    } else {
        Serial.printf("[LoRaWAN] join failed, code %d (retry in %d s)\n", st, JOIN_RETRY_S);
    }
}

// ============================ Serial commands ============================
static void handleSerial() {
    static String in;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (in.length()) {
                long v = in.toInt();
                if (in == "g") {                       // 'g' + Enter: show / hide the raw GPS sentences
                    echoNmea = !echoNmea;
                    Serial.printf("raw GPS sentences %s\n", echoNmea ? "ON" : "OFF");
                } else if (v >= MIN_CYCLE_S) {
                    cycleSeconds = v;
                    Serial.printf("cycle set to %lu s\n", (unsigned long)cycleSeconds);
                } else {
                    Serial.printf("ignored: type a number of seconds >= %d\n", MIN_CYCLE_S);
                }
                in = "";
            }
        } else if (in.length() < 8) {
            in += c;
        }
    }
}

// ============================ Arduino entry points ============================
void setup() {
    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 3000;) delay(10);   // wait for USB host
    Serial.println("\n=== Satlink station boot ===");

    Wire1.begin(I2C1_SDA, I2C1_SCL);
    pmuOk = setupPower();
    Serial.printf("PMU AXP2101 : %s\n", pmuOk ? "OK" : "FAIL");
    delay(100);

    Serial1.setRxBufferSize(2048);   // LoRaWAN RX windows block for seconds: do not lose GPS bytes meanwhile
    Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    delay(500);
    gpsModel = detectGps();
    configureGps(gpsModel);

    bmeOk = setupBme();
    Serial.printf("BME280      : %s\n", bmeOk ? "OK" : "FAIL");
    gasBegin();
    Serial.printf("Gas MICS6814: %s\n", gasPresent ? "OK (raw mV, heater warm-up needed)" : "not detected (check wiring)");
    bool oled = oledBegin();
    Serial.printf("OLED        : %s\n", oled ? "OK (BOOT button = next page)" : "not found");
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    sdOk = setupSd();
    Serial.printf("microSD     : %s\n", sdOk ? "OK" : "not present (continuing without it)");
    radioOk = setupLoRaWAN();
    Serial.printf("LoRa SX1262 : %s\n", radioOk ? "OK" : "FAIL");
    Serial.printf("Cycle       : %lu s (type a number + Enter to change)\n", (unsigned long)cycleSeconds);
    // Nothing above halts the program: in flight a dead sensor must not stop the others.
}

void loop() {
    while (Serial1.available()) {                             // keep feeding the NMEA parser
        char c = Serial1.read();
        gps.encode(c);
        if (echoNmea) Serial.write(c);
    }
    handleSerial();

    // BOOT button -> next screen page; screen redraw once per second from the latest cycle data
    static bool btnPrev = true;
    static uint32_t lastDraw = 0, lastPageChange = 0;
    bool btn = digitalRead(BOOT_BTN_PIN);
    if (!btn && btnPrev) { oledNextPage(); lastPageChange = millis(); lastDraw = 0; }
    btnPrev = btn;
    if (OLED_PAGE_SECONDS > 0 && millis() - lastPageChange > OLED_PAGE_SECONDS * 1000UL) { oledNextPage(); lastPageChange = millis(); lastDraw = 0; }
    if (oledOk && millis() - lastDraw > 1000) { lastDraw = millis(); oledDraw(scr); }

    static uint32_t lastCycle = 0, lastJoinTry = 0, lastSdTry = 0;
    if (lastCycle != 0 && millis() - lastCycle < cycleSeconds * 1000UL) return;
    lastCycle = millis();
    if (lastCycle == 0) lastCycle = 1;

    // ---- late SD insertion ----
    if (!sdOk && millis() - lastSdTry > SD_RETRY_S * 1000UL) {
        lastSdTry = millis();
        sdOk = setupSd();
        if (sdOk) Serial.println("microSD     : card detected, logging started");
    }

    // ---- join (blocks a few seconds; GPS bytes keep buffering) ----
    if (radioOk && keysOk && !joined && (lastJoinTry == 0 || millis() - lastJoinTry > JOIN_RETRY_S * 1000UL)) {
        lastJoinTry = millis();
        tryJoin();
    }

    // ---- acquire ----
    Telemetry t = {};
    bool fix = gps.location.isValid() && gps.location.age() < 3000;
    float tempC = NAN, hum = NAN, pressHpa = NAN;
    if (bmeOk) {
        tempC = bme.readTemperature();
        hum = bme.readHumidity();
        pressHpa = bme.readPressure() / 100.0f;
    }
    GasReading gas = gasRead();
    uint16_t vbat = (pmuOk && pmu.isBatteryConnect()) ? pmu.getBattVoltage() : 0;

    t.lat_e6 = fix ? (int32_t)lround(gps.location.lat() * 1e6) : 0;
    t.lon_e6 = fix ? (int32_t)lround(gps.location.lng() * 1e6) : 0;
    double alt = gps.altitude.isValid() ? gps.altitude.meters() : 0;
    t.alt_m = (uint16_t)constrain((long)lround(alt), 0L, 65535L);
    t.temp_cC = isnan(tempC) ? INT16_MIN : (int16_t)lroundf(tempC * 100.0f);
    t.hum_cPct = isnan(hum) ? 0xFFFF : (uint16_t)lroundf(hum * 100.0f);
    t.press_dhPa = isnan(pressHpa) ? 0xFFFF : (uint16_t)lroundf(pressHpa * 10.0f);
    t.vbat_mV = vbat;
    t.sats = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
    static uint32_t lastGpsChars = 0;                       // GPS "active" = NMEA bytes arrived since last cycle
    bool gpsActive = gps.charsProcessed() != lastGpsChars;
    lastGpsChars = gps.charsProcessed();
    t.flags = (fix ? 1 : 0) | (bmeOk ? 2 : 0) | (sdOk ? 4 : 0) | (joined ? 8 : 0) | (gpsActive ? 16 : 0)
              | (gasPresent ? 32 : 0) | (gasWarm() ? 64 : 0);
    t.co_mV = gas.co_mV; t.no2_mV = gas.no2_mV; t.nh3_mV = gas.nh3_mV;

    char utc[12] = "--:--:--";
    if (gps.time.isValid())
        snprintf(utc, sizeof(utc), "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());

    // ---- transmit ----
    int16_t loraState = -1;
    if (radioOk && joined) {
        uint8_t down[32];
        size_t downLen = sizeof(down);
        loraState = node.sendReceive((uint8_t *)&t, sizeof(t), LORAWAN_FPORT, down, &downLen);
        if (loraState == RADIOLIB_ERR_NETWORK_NOT_JOINED) joined = false;
        if (loraState > 0 && downLen > 0) {            // a downlink arrived (Rx window 1 or 2)
            Serial.print("[LoRaWAN] downlink: ");
            for (size_t i = 0; i < downLen; i++) Serial.printf("%02X", down[i]);
            Serial.println();
        }
    }

    scr.fix = fix; scr.bmeOk = bmeOk; scr.sdOk = sdOk; scr.joined = joined; scr.keysOk = keysOk; scr.radioOk = radioOk;
    scr.sats = t.sats; scr.lat = gps.location.lat(); scr.lon = gps.location.lng(); scr.altGps = alt;
    scr.tempC = tempC; scr.hum = hum; scr.pressHpa = pressHpa; scr.vbatMv = vbat;
    scr.gas = gas; scr.gasPresent = gasPresent; scr.gasWarm = gasWarm();
    scr.loraState = loraState; scr.cycleS = cycleSeconds;
    memcpy(scr.utc, utc, sizeof(utc));

    // ---- log: Serial + SD (same line) ----
    char line[240];
    snprintf(line, sizeof(line), "%lu,%s,%d,%u,%.6f,%.6f,%.1f,%.2f,%.2f,%.2f,%u,%d,%d,%d,%d,%d",
             (unsigned long)millis(), utc, fix ? 1 : 0, t.sats,
             fix ? gps.location.lat() : 0.0, fix ? gps.location.lng() : 0.0, alt,
             tempC, hum, pressHpa, vbat, joined ? 1 : 0, loraState,
             gas.co_mV == GAS_NONE ? -1 : gas.co_mV, gas.no2_mV == GAS_NONE ? -1 : gas.no2_mV, gas.nh3_mV == GAS_NONE ? -1 : gas.nh3_mV);
    Serial.println(line);
    if (!fix)   // GPS health: chars=0 -> no data (wiring/power); bad>>ok -> baud problem; ok growing -> module talks, no satellites yet
        Serial.printf("# GPS no fix: chars=%lu sentences_ok=%lu sentences_bad=%lu\n",
                      (unsigned long)gps.charsProcessed(), (unsigned long)gps.passedChecksum(),
                      (unsigned long)gps.failedChecksum());
    if (sdOk) {
        if (logFile.println(line) == 0) sdOk = false;   // card pulled: stop writing, keep running
        else logFile.flush();
    }
}
