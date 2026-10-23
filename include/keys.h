#pragma once
// ============================================================================
//  ITPA LoRaWAN KEYS  -  the ONLY file you need to touch tomorrow.
// ============================================================================
//  Paste each value between the quotes, exactly as the organizers wrote it:
//   - hex digits only (0-9, A-F), WITHOUT the "0x"
//   - spaces, dashes and colons are allowed and ignored
//  Never share this file: the AppKey is secret.
// ============================================================================

#define LORAWAN_DEVEUI   "F19424C168C6D04D"      // 16 characters - from the organizers (done)
#define LORAWAN_JOINEUI  "B0EB16A55A9EF6AF"      // 16 characters - from the organizers (done)

// >>> TOMORROW: paste the AppKey here. It must be 32 characters (the message you got had only 16). <<<
#define LORAWAN_APPKEY   ""

// The ITPA/course network is LoRaWAN 1.0.x: leave this at 0 unless the organizers say 1.1.
#define LORAWAN_USE_1_1  0
#define LORAWAN_NWKKEY   ""                      // only used when LORAWAN_USE_1_1 is 1 (32 characters)
