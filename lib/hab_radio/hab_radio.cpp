#include "hab_radio.h"

#include <SPI.h>

#include "pins.h"
#include "radio_config.h"

int habRadioBegin(SX1262& radio) {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

  // Heltec WiFi LoRa 32 V3: onboard TCXO is 1.8 V (RadioLib default 1.6 is wrong).
  // Pass full PHY into begin so TCXO is applied during modSetup, before PA config.
  int state = radio.begin(
      LORA_FREQUENCY_HZ / 1e6f,
      LORA_BW_KHZ,
      static_cast<uint8_t>(LORA_SF),
      static_cast<uint8_t>(LORA_CR),
      static_cast<uint8_t>(LORA_SYNC_WORD),
      static_cast<int8_t>(LORA_TX_POWER_DBM),
      static_cast<uint16_t>(LORA_PREAMBLE_LEN),
      1.8f,   // TCXO voltage
      false); // DC-DC (not LDO)
  if (state != RADIOLIB_ERR_NONE) {
    return state;
  }

  // RadioLib begin() defaults OCP to 60 mA — too low for ~22 dBm on SX1262.
  state = radio.setCurrentLimit(140.0f);
  if (state != RADIOLIB_ERR_NONE) {
    return state;
  }

  state = radio.setDio2AsRfSwitch(true);
  if (state != RADIOLIB_ERR_NONE) {
    return state;
  }

  state = radio.setCRC(true);
  if (state != RADIOLIB_ERR_NONE) {
    return state;
  }

  return RADIOLIB_ERR_NONE;
}

int habRadioApplyConfig(SX1262& radio) {
  int state;

  state = radio.setFrequency(LORA_FREQUENCY_HZ / 1e6f);
  if (state != RADIOLIB_ERR_NONE) return state;

  state = radio.setSpreadingFactor(LORA_SF);
  if (state != RADIOLIB_ERR_NONE) return state;

  state = radio.setBandwidth(LORA_BW_KHZ);
  if (state != RADIOLIB_ERR_NONE) return state;

  state = radio.setCodingRate(LORA_CR);
  if (state != RADIOLIB_ERR_NONE) return state;

  state = radio.setOutputPower(LORA_TX_POWER_DBM);
  if (state != RADIOLIB_ERR_NONE) return state;

  state = radio.setPreambleLength(LORA_PREAMBLE_LEN);
  if (state != RADIOLIB_ERR_NONE) return state;

  state = radio.setSyncWord(LORA_SYNC_WORD);
  if (state != RADIOLIB_ERR_NONE) return state;

  state = radio.setCRC(true);
  if (state != RADIOLIB_ERR_NONE) return state;

  state = radio.setCurrentLimit(140.0f);
  if (state != RADIOLIB_ERR_NONE) return state;

  return RADIOLIB_ERR_NONE;
}

void habRadioPrintConfig(Print& out) {
  out.printf("LoRa cfg: %.3f MHz  SF%d  BW=%.0f kHz  CR=4/%d  P=%d dBm  sync=0x%02X\n",
             LORA_FREQUENCY_HZ / 1e6f,
             LORA_SF,
             (double)LORA_BW_KHZ,
             LORA_CR,
             LORA_TX_POWER_DBM,
             LORA_SYNC_WORD);
}
