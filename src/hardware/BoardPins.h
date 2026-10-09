#pragma once

// Waveshare ESP32-S3-Touch-AMOLED-1.75 (see the board's HARDWARE_REFERENCE.md).
constexpr int DISPLAY_CHIP_SELECT_PIN = 12;
constexpr int DISPLAY_CLOCK_PIN = 38;
constexpr int DISPLAY_DATA0_PIN = 4;
constexpr int DISPLAY_DATA1_PIN = 5;
constexpr int DISPLAY_DATA2_PIN = 6;
constexpr int DISPLAY_DATA3_PIN = 7;
constexpr int DISPLAY_RESET_PIN = 39;
constexpr int DISPLAY_COLUMN_OFFSET = 6;  // the CO5300's visible area starts at column 6
constexpr int DISPLAY_TEARING_PIN = 13;   // CO5300 TE output (LCD_TE on the schematic)

constexpr int I2C_DATA_PIN = 15;
constexpr int I2C_CLOCK_PIN = 14;

// Expansion header pin 8: UART1 receive, from the power bus data provider.
constexpr int BUS_DATA_PIN = 16;
