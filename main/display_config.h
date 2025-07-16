#pragma once

#include "driver/gpio.h"

// ILI9488 Display Configuration
#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 320

// SPI Configuration
#define SPI_HOST_ID    SPI2_HOST

// TFT Display GPIO Pins
#define TFT_CS_PIN     GPIO_NUM_10
#define TFT_RST_PIN    GPIO_NUM_6
#define TFT_DC_PIN     GPIO_NUM_7
#define TFT_MOSI_PIN   GPIO_NUM_11
#define TFT_SCLK_PIN   GPIO_NUM_12
#define TFT_MISO_PIN   GPIO_NUM_13
#define TFT_BL_PIN     GPIO_NUM_5

// XPT2046 Touch Controller GPIO Pins
#define TOUCH_CS_PIN   GPIO_NUM_4
#define TOUCH_IRQ_PIN  GPIO_NUM_3

// SPI Frequency Settings
#define SPI_FREQ_MHZ   40  // 40MHz for display
#define TOUCH_FREQ_MHZ 2.5 // 2.5MHz for touch controller