#pragma once

#include <stdio.h>

// =============================================================================
// EDIT THESE BEFORE REFLASHING — flash BOTH payload and ground with the same
// values so the link-budget run uses a matched PHY on both ends.
// =============================================================================

// Named preset (optional). Uncomment ONE, or leave all commented and set knobs
// below manually.
// #define LORA_PRESET_SF7_22
// #define LORA_PRESET_SF10_22
// #define LORA_PRESET_SF12_14

#if defined(LORA_PRESET_SF7_22)
  #define LORA_FREQUENCY_HZ   915000000UL
  #define LORA_SF             7
  #define LORA_BW_KHZ         125.0f
  #define LORA_CR             5          // 4/5
  #define LORA_TX_POWER_DBM   22
#elif defined(LORA_PRESET_SF12_14)
  #define LORA_FREQUENCY_HZ   915000000UL
  #define LORA_SF             12
  #define LORA_BW_KHZ         125.0f
  #define LORA_CR             5
  #define LORA_TX_POWER_DBM   14
#else
  // Default / LORA_PRESET_SF10_22 — starting link-budget point
  #ifndef LORA_FREQUENCY_HZ
    #define LORA_FREQUENCY_HZ 915000000UL   // Hz  (Canada 902–928 MHz ISM)
  #endif
  #ifndef LORA_SF
    #define LORA_SF           10            // Spreading factor 7–12
  #endif
  #ifndef LORA_BW_KHZ
    #define LORA_BW_KHZ       125.0f        // Bandwidth kHz (125 / 250 / 500)
  #endif
  #ifndef LORA_CR
    #define LORA_CR           5             // Coding rate denominator: 5=4/5 … 8=4/8
  #endif
  #ifndef LORA_TX_POWER_DBM
    #define LORA_TX_POWER_DBM 22            // dBm (Heltec SX1262 up to ~22)
  #endif
#endif

#ifndef LORA_PREAMBLE_LEN
  #define LORA_PREAMBLE_LEN   8
#endif

// RadioLib private sync word (avoid LoRaWAN public 0x34)
#ifndef LORA_SYNC_WORD
  #define LORA_SYNC_WORD      0x12
#endif

// Cadence (payload)
#ifndef HAB_TELEM_INTERVAL_MS
  #define HAB_TELEM_INTERVAL_MS 1000UL  // match onboard 1 Hz sample / SD log
#endif
#ifndef HAB_SAMPLE_INTERVAL_MS
  #define HAB_SAMPLE_INTERVAL_MS 1000UL
#endif

// Ground ping ACK wait
#ifndef HAB_PING_TIMEOUT_MS
  #define HAB_PING_TIMEOUT_MS 5000UL
#endif

// Compact PHY id for session folder names, e.g. "sf10_bw125_cr5_p22_915".
static inline char* habRadioPhySlug(char* buf, size_t cap) {
  if (!buf || cap == 0) {
    return buf;
  }
  const unsigned mhz = (unsigned)(LORA_FREQUENCY_HZ / 1000000UL);
  snprintf(buf, cap, "sf%u_bw%.0f_cr%u_p%d_%u", (unsigned)LORA_SF,
           (double)LORA_BW_KHZ, (unsigned)LORA_CR, (int)LORA_TX_POWER_DBM, mhz);
  return buf;
}
