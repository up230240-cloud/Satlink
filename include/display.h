#pragma once
// SH1106 128x64 OLED on the T-Beam Supreme (I2C0, address 0x3C or 0x3D). Pages: 0 GPS, 1 weather, 2 gas, 3 radio/system.
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "settings.h"
#include "gas.h"

struct ScreenData {
    bool fix, bmeOk, sdOk, joined, keysOk, radioOk;
    uint8_t sats;
    double lat, lon, altGps;
    float tempC, hum, pressHpa;
    uint16_t vbatMv;
    GasReading gas;
    bool gasPresent, gasWarm;
    int16_t loraState;
    uint32_t cycleS;
    char utc[12];
};

#if OLED_ENABLED

static U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
static bool oledOk = false;
static uint8_t oledPage = 0;
#define OLED_PAGES 4

static bool oledBegin() {   // call after Wire.begin(17, 18)
    uint8_t addr = 0;
    Wire.beginTransmission(0x3D); if (Wire.endTransmission() == 0) addr = 0x3D;   // 0x3C may be the magnetometer
    if (!addr) { Wire.beginTransmission(0x3C); if (Wire.endTransmission() == 0) addr = 0x3C; }
    if (!addr) return false;
    oled.setI2CAddress(addr << 1);
    oled.begin();
    oled.setFont(u8g2_font_6x10_tf);
    oledOk = true;
    return true;
}

static void oledNextPage() { oledPage = (oledPage + 1) % OLED_PAGES; }

static void oledMv(char *dst, size_t n, const char *name, uint16_t mv, bool warm) {
    if (mv == GAS_NONE) snprintf(dst, n, "%-4s --", name);
    else snprintf(dst, n, "%-4s %4u mV%s", name, mv, warm ? "" : " *");
}

static void oledDraw(const ScreenData &d) {
    if (!oledOk) return;
    char l[32];
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
    switch (oledPage) {
    case 0:
        oled.drawStr(0, 9, "GPS");
        snprintf(l, sizeof(l), "%s S:%u", d.fix ? "FIX" : "no fix", d.sats);
        oled.drawStr(40, 9, l);
        snprintf(l, sizeof(l), "UTC %s", d.utc);
        oled.drawStr(0, 21, l);
        if (d.fix) {
            snprintf(l, sizeof(l), "La %.5f", d.lat); oled.drawStr(0, 33, l);
            snprintf(l, sizeof(l), "Lo %.5f", d.lon); oled.drawStr(0, 45, l);
            snprintf(l, sizeof(l), "Alt %.0f m", d.altGps); oled.drawStr(0, 57, l);
        } else {
            oled.drawStr(0, 36, "Searching sky...");
        }
        break;
    case 1:
        oled.drawStr(0, 9, "WEATHER (BME280)");
        if (d.bmeOk) {
            snprintf(l, sizeof(l), "Temp  %.1f C", d.tempC);   oled.drawStr(0, 24, l);
            snprintf(l, sizeof(l), "Hum   %.1f %%", d.hum);    oled.drawStr(0, 36, l);
            snprintf(l, sizeof(l), "Press %.1f hPa", d.pressHpa); oled.drawStr(0, 48, l);
        } else {
            oled.drawStr(0, 30, "sensor not found");
        }
        if (d.vbatMv) snprintf(l, sizeof(l), "Batt %.2f V", d.vbatMv / 1000.0f);
        else snprintf(l, sizeof(l), "Batt none");
        oled.drawStr(0, 60, l);
        break;
    case 2:
        oled.drawStr(0, 9, "GAS MICS-6814");
        if (!d.gasPresent) {
            oled.drawStr(0, 30, "not detected");
        } else {
            oledMv(l, sizeof(l), "CO",  d.gas.co_mV,  d.gasWarm); oled.drawStr(0, 24, l);
            oledMv(l, sizeof(l), "NO2", d.gas.no2_mV, d.gasWarm); oled.drawStr(0, 36, l);
            oledMv(l, sizeof(l), "NH3", d.gas.nh3_mV, d.gasWarm); oled.drawStr(0, 48, l);
            oled.drawStr(0, 60, d.gasWarm ? "raw mV" : "* WARM-UP");
        }
        break;
    default:
        oled.drawStr(0, 9, "RADIO / SYSTEM");
        if (!d.radioOk) snprintf(l, sizeof(l), "LoRa: FAIL");
        else if (!d.keysOk) snprintf(l, sizeof(l), "LoRaWAN: no AppKey");
        else if (!d.joined) snprintf(l, sizeof(l), "LoRaWAN: joining");
        else snprintf(l, sizeof(l), "LoRaWAN: joined");
        oled.drawStr(0, 24, l);
        snprintf(l, sizeof(l), "Last TX: %d", d.loraState); oled.drawStr(0, 36, l);
        snprintf(l, sizeof(l), "SD:%s  Cycle:%lus", d.sdOk ? "ok" : "no", (unsigned long)d.cycleS); oled.drawStr(0, 48, l);
        snprintf(l, sizeof(l), "Page %u/%u (BOOT)", oledPage + 1, OLED_PAGES); oled.drawStr(0, 60, l);
        break;
    }
    oled.sendBuffer();
}

#else

struct DummyOled {};
static bool oledOk = false;
static bool oledBegin() { return false; }
static void oledNextPage() {}
static void oledDraw(const ScreenData &) {}

#endif
