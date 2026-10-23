#pragma once
// MICS-6814 module with 3 analog outputs (CO / NO2 / NH3). We report RAW millivolts only:
// converting to ppm needs the module's load resistor and a clean-air calibration (R0).
#include <Arduino.h>
#include <Wire.h>
#include "settings.h"

#define GAS_NONE 0xFFFF

struct GasReading {
    uint16_t co_mV  = GAS_NONE;   // millivolts at the MODULE output (divider ratio already undone)
    uint16_t no2_mV = GAS_NONE;
    uint16_t nh3_mV = GAS_NONE;
};

static bool gasPresent = false;
static uint32_t gasStartMs = 0;

#if GAS_ENABLED && GAS_BACKEND == GAS_BACKEND_ADS1115

// Single-shot read of one ADS1115 input, +-4.096 V range (0.125 mV per step). Returns -1 on error.
static int32_t adsReadMv(uint8_t input) {
    uint16_t cfg = 0x8000                          // start conversion
                 | ((uint16_t)(0x04 + input) << 12) // MUX: single-ended AINx vs GND
                 | (0x01 << 9)                      // PGA +-4.096 V
                 | (1 << 8)                         // single-shot
                 | (0x04 << 5)                      // 128 samples/s
                 | 0x03;                            // comparator off
    Wire.beginTransmission(GAS_ADS_ADDR);
    Wire.write(0x01);
    Wire.write(cfg >> 8);
    Wire.write(cfg & 0xFF);
    if (Wire.endTransmission() != 0) return -1;
    delay(10);                                     // 128 SPS = 7.8 ms
    Wire.beginTransmission(GAS_ADS_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return -1;
    if (Wire.requestFrom((int)GAS_ADS_ADDR, 2) != 2) return -1;
    int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());
    if (raw < 0) raw = 0;
    return (int32_t)raw * 125 / 1000;              // millivolts at the ADS input
}

static bool gasBegin() {
    Wire.beginTransmission(GAS_ADS_ADDR);
    gasPresent = (Wire.endTransmission() == 0);
    gasStartMs = millis();
    return gasPresent;
}

static GasReading gasRead() {
    GasReading g;
    if (!gasPresent) return g;
    int32_t v;
    if ((v = adsReadMv(GAS_ADS_IN_CO))  >= 0) g.co_mV  = (uint16_t)constrain((long)(v * GAS_DIVIDER_RATIO), 0L, 65534L);
    if ((v = adsReadMv(GAS_ADS_IN_NO2)) >= 0) g.no2_mV = (uint16_t)constrain((long)(v * GAS_DIVIDER_RATIO), 0L, 65534L);
    if ((v = adsReadMv(GAS_ADS_IN_NH3)) >= 0) g.nh3_mV = (uint16_t)constrain((long)(v * GAS_DIVIDER_RATIO), 0L, 65534L);
    return g;
}

#elif GAS_ENABLED && GAS_BACKEND == GAS_BACKEND_GPIO3

// Quick test: ONE output straight into GPIO3 (the only free ADC pin on the header). Max 3.3 V!
static bool gasBegin() {
    pinMode(3, INPUT);
    gasPresent = true;
    gasStartMs = millis();
    return true;
}

static GasReading gasRead() {
    GasReading g;
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(3);
    uint16_t mv = (uint16_t)constrain((long)((sum / 16) * GAS_DIVIDER_RATIO), 0L, 65534L);
    if (GAS_GPIO3_GAS == 0) g.co_mV = mv;
    else if (GAS_GPIO3_GAS == 1) g.no2_mV = mv;
    else g.nh3_mV = mv;
    return g;
}

#else   // gas sensor disabled

static bool gasBegin() { return false; }
static GasReading gasRead() { return GasReading(); }

#endif

static bool gasWarm() { return gasPresent && (millis() - gasStartMs) >= (uint32_t)GAS_WARMUP_S * 1000UL; }
