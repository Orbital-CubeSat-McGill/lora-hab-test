#pragma once

#include <stdint.h>

// Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) pin map
// Onboard LoRa / OLED left at Heltec defaults.

// --- Onboard SX1262 (RadioLib Module NSS, DIO1, RST, BUSY) ---
static const int PIN_LORA_NSS  = 8;
static const int PIN_LORA_SCK  = 9;
static const int PIN_LORA_MOSI = 10;
static const int PIN_LORA_MISO = 11;
static const int PIN_LORA_RST  = 12;
static const int PIN_LORA_BUSY = 13;
static const int PIN_LORA_DIO1 = 14;

// --- Onboard OLED (SSD1306) I2C — NOT on the pin headers ---
static const int PIN_OLED_SDA = 17;
static const int PIN_OLED_SCL = 18;
static const int PIN_OLED_RST = 21;

// --- External I2C on headers (MPU6050 + QMC5883P); Heltec Arduino SDA/SCL ---
static const int PIN_I2C_SDA = 41;
static const int PIN_I2C_SCL = 42;

// --- MicroSD on second SPI bus (not LoRa's SPI) ---
static const int PIN_SD_CS   = 4;
static const int PIN_SD_SCK  = 7;
static const int PIN_SD_MOSI = 6;
static const int PIN_SD_MISO = 5;

// --- Neo-6M GPS UART1 ---
static const int PIN_GPS_RX = 47;  // ESP RX <- GPS TX
static const int PIN_GPS_TX = 48;  // ESP TX -> GPS RX

// --- Board controls ---
static const int PIN_PRG_BTN = 0;   // PRG / BOOT button (active LOW)
static const int PIN_VEXT    = 36;  // peripheral power, active LOW

// I2C addresses
static const uint8_t I2C_ADDR_OLED    = 0x3C;
static const uint8_t I2C_ADDR_MPU6050 = 0x68;
static const uint8_t I2C_ADDR_QMC5883P = 0x2C;
