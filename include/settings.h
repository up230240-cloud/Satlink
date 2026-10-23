#pragma once
// ============================================================================
//  SETTINGS you may want to change. (The ITPA keys live in keys.h.)
// ============================================================================

// ---- Timing / LoRaWAN -------------------------------------------------------
#define CYCLE_SECONDS_DEFAULT  5       // seconds between readings and uplinks (can also be changed live: type a number + Enter)
#define MIN_CYCLE_S            5
#define LORAWAN_DR             3       // US915 uplink data rate: 3 = SF7 (fast) ... 1 = SF9 (longer range). DR0 is too small for our payload.
#define LORAWAN_FPORT          1
#define JOIN_RETRY_S           60      // seconds between join attempts while not joined
#define SD_RETRY_S             30      // seconds between attempts to mount a card inserted later
#define LOG_FILE               "/telemetry.csv"
#define GPS_L76K_AIRBORNE      0       // 1 = send $PCAS11,5 (airborne mode) to the L76K. Leave 0 until the mode number is confirmed.

// ---- Gas sensor MICS-6814 (3 analog outputs: CO / NO2 / NH3) ----------------
#define GAS_ENABLED            1       // 0 = ignore the gas sensor completely
#define GAS_BACKEND_ADS1115    1       // 3 channels through an ADS1115 I2C converter (recommended)
#define GAS_BACKEND_GPIO3      2       // ONE channel straight into pin GPIO3 (quick test without the ADS1115)
#define GAS_BACKEND            GAS_BACKEND_ADS1115

#define GAS_ADS_ADDR           0x48    // ADS1115 with its ADDR pin on GND
#define GAS_ADS_IN_CO          0       // which ADS1115 input (A0..A3) each module output is wired to
#define GAS_ADS_IN_NO2         1
#define GAS_ADS_IN_NH3         2

#define GAS_GPIO3_GAS          0       // backend GPIO3 only: which output is on GPIO3 -> 0 = CO, 1 = NO2, 2 = NH3

#define GAS_DIVIDER_RATIO      1.0f    // 1.0 = module output wired straight in. If you add a 2-resistor divider, use (R1+R2)/R2 (two equal resistors = 2.0)
#define GAS_WARMUP_S           180     // the heater needs minutes to settle; the screen shows WARM-UP until then

// ---- Screen (the SH1106 OLED on the board) ----------------------------------
#define OLED_ENABLED           1
#define OLED_PAGE_SECONDS      4       // auto-change page every N seconds (0 = only with the BOOT button). BOOT button = next page.
