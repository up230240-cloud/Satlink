#pragma once
#include <Arduino.h>
#include <math.h>

// Parses a hex string into out[] (first maxBytes bytes). Spaces, ':', '-', ',' and tabs are ignored.
// Returns the number of hex DIGITS found (a full 16-byte key = 32), or -1 on any other character
// (for example the "x" of a 0x prefix).
inline int hexToBytes(const char *s, uint8_t *out, size_t maxBytes) {
    int digits = 0;
    for (const char *p = s; *p; ++p) {
        char c = *p;
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c == ' ' || c == ':' || c == '-' || c == ',' || c == '\t') continue;
        else return -1;
        size_t idx = (size_t)digits / 2;
        if (idx < maxBytes) {
            if (digits % 2 == 0) out[idx] = (uint8_t)(v << 4);
            else out[idx] |= (uint8_t)v;
        }
        digits++;
    }
    return digits;
}

// 16 hex digits -> 64-bit number, most significant byte first (the way ChirpStack shows EUIs).
inline bool parseHexU64(const char *s, uint64_t *out) {
    uint8_t b[8] = {0};
    if (hexToBytes(s, b, 8) != 16) return false;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | b[i];
    *out = v;
    return true;
}

// ISA standard atmosphere, valid up to 20 km. sea-level pressure in hPa.
inline float baroAltitudeM(float pHpa, float seaLevelHpa = 1013.25f) {
    if (pHpa >= 226.32f) return 44330.77f * (1.0f - powf(pHpa / seaLevelHpa, 0.190263f));
    return 11000.0f - 6341.62f * logf(pHpa / 226.32f);
}
