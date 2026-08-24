#pragma once

// TFT 2.4/2.8 inch 320x240 dengan driver ILI9341.
#define USER_SETUP_INFO "NodeMCU-LED ILI9341"
#define ILI9341_DRIVER

// Hardware SPI NodeMCU ESP8266.
#define TFT_MISO PIN_D6
#define TFT_MOSI PIN_D7
#define TFT_SCLK PIN_D5
#define TFT_CS   PIN_D8
#define TFT_DC   PIN_D3

// Sambungkan pin RST TFT ke pin RST NodeMCU.
// D4/GPIO2 dipertahankan untuk kontrol backlight seperti project lama.
#define TFT_RST -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

#define SPI_FREQUENCY 27000000
