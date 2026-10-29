#pragma once

#include <RadioLib.h>

// Returns RadioLib status code (RADIOLIB_ERR_NONE on success).
int habRadioBegin(SX1262& radio);

// Apply compile-time PHY from radio_config.h (call after begin).
int habRadioApplyConfig(SX1262& radio);

// Human-readable summary of current compile-time knobs.
void habRadioPrintConfig(Print& out);
