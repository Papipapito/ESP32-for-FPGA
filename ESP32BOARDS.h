/*
ESP32BOARDS.h
    ESP32 UNAPI Implementation.
    Revision 1.00

Requires Arduino IDE and ESP32 libraries

Copyright (c) 2019 - 2026 Oduvaldo Pavan Junior ( ducasp@ gmail.com )
Copyright (c) 2026 - Leo Manes ( https://github.com/leomanes )
All rights reserved.

If you integrate this on your hardware, please consider the 
possibility of sending one piece of it as a thank you to the author :)
Of course this is not mandatory, if you like the idea, contact the
author in the e-mail address above.

This file, that is part of ESP8266 UNAPI Firmware program, is free
software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free
Software Foundation, either version 2.1 of the License, or (at your
option) any later version.

This file is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this file.  If not, see <https://www.gnu.org/licenses/>

Edit this file to choose your target processor and FLASH size
If for some reason you are using a target not here, add the new
target following the examples of the existing targets and request
an update on the main repository please.

I do not recommend using 4M flash as the firmware for most platforms
currently has over 1M (and considering space for OTA and file system)
leaving little room for new features.

Also edit this file to select the LED behavior you want and the GPIO
PIN and type of led use.
*/

#ifndef _ESP32BOARDS_H
#define _ESP32BOARDS_H

enum SupportedBaudRates {
  BR9600 = 0,
  BR19200 = 1,
  BR57600 = 2,
  BR115200 = 3,
  BR230400 = 4,
  BR460800 = 5,
  BR921600 = 6,
  BR859372 = 7
};

//Uncomment ONLY the ESP Model you are targetting
// 17/09/2026: la placa se elige desde la linea de ordenes (arduino-cli ... compiler.cpp.extra_flags=-DESP32_S3);
// sin nada es el C6 (Waveshare ESP32-C6-LCD-1.3). ESP32_S3 = ESP32-1732S019 (S3 + LCD 1.9" 170x320).
#if !defined(ESP32_S3) && !defined(ESP32_WROOM) && !defined(ESP32_C6)
#define ESP32_C6
#endif

//Choose the default baud rate
//Notice that if replacing a legacy ESP-01 on MSX FPGA or MSX PICO, the default is 859372 for it
//Notice that in my tests 859372 did not work nicely with ESP32-WROOM but worked fine with ESP32C6 and S3
//921600 should work nice on all of them
#ifdef ESP32_WROOM
  #define ESP32BAUDRATE BR921600
#else
  #define ESP32BAUDRATE BR859372
#endif

//Uncomment ONLY the Flash Size you are targetting
//#define FLASH_8M
//#define FLASH_16M
#ifdef ESP32_S3
#define FLASH_16M           // ESP32-1732S019 = modulo N16R8 (16 MB; la tabla partitions.csv del sketch usa 4 MB)
#else
#define FLASH_4M            // Waveshare ESP32-C6-LCD-1.3 = 4MB (particion Huge APP No-OTA en Arduino IDE)
#endif

#ifdef ESP32_S3
  #ifdef FLASH_8M
    #define FIRMWARETYPE "UN32S308"
  #endif
  #ifdef FLASH_16M
    #define FIRMWARETYPE "UN32S316"
  #endif
#endif

#ifdef ESP32_C6
  #ifdef FLASH_4M
    #define FIRMWARETYPE "UN32C604"
  #endif
  #ifdef FLASH_8M
    #define FIRMWARETYPE "UN32C608"
  #endif
  #ifdef FLASH_16M
    #define FIRMWARETYPE "UN32C616"
  #endif
#endif

#ifdef ESP32_WROOM
  #ifdef FLASH_8M
    #define FIRMWARETYPE "UN32WR08"
  #endif
  #ifdef FLASH_16M
    #define FIRMWARETYPE "UN32WR16"
  #endif
#endif

// ======================== ENLACE CON EL FPGA (MSX <-> ESP) ========================
// El sketch habla con el FPGA por MSXLINK; en el C6 es el UART0 (Serial, pines 16/17 de la placa).
// En la ESP32-1732S019 el UART0 (GPIO43/44) es del CH340 del USB-C, asi que el enlace va por el
// UART1 en dos GPIO del header P2 y el Serial queda libre como consola de depuracion (115200).
//   ZYNQ MINI (CAM1)         ESP32-1732S019 (header P2, de arriba abajo: 2 42 41 40 39 38 45 48 47 21 GND 5V)
//   26 W16 esp_rx_i   <----  GPIO40 (TX del ESP)
//   28 R18 esp_tx_o   ---->  GPIO39 (RX del ESP)
//   30 P19 esp_turbo  ---->  GPIO41 (entrada, pull-down)
//   37/38 GND         -----  GND (P2, penultimo por abajo)
//   Alimentacion: el USB-C de la S3 (a un USB-A de la Zynq o a un cargador). NUNCA unir los 3V3.
#ifdef ESP32_S3
  #define MSXLINK          Serial1
  #define MSXLINK_RX_PIN   39
  #define MSXLINK_TX_PIN   40
  #define MSXLINK_BEGIN(b) Serial1.begin((b), SERIAL_8N1, MSXLINK_RX_PIN, MSXLINK_TX_PIN)
  #define TURBO_PIN        41
  #define HAVE_CONSOLE     1     // Serial = CH340 (USB-C): logs a 115200
#else
  #define MSXLINK          Serial
  #define MSXLINK_BEGIN(b) Serial.begin(b)
  #define TURBO_PIN        3     // GPIO libre del C6 (header) cableado al pin de turbo del FPGA
#endif

// ======================== ONBOARD WIFI-CONNECTED LED ========================
// Blue led if STA has an IP. "WiFi up"
// firmware OTA it should reconnect on its own after reboot
//
// Configure for your board:
//   WIFI_LED_PIN   pin number (LED_BUILTIN if WS2812 RGB on GPIO 48)
//   WIFI_LED_RGB   set to 1 if the onboard LED is a WS2812
//   WIFI_LED_ON    HIGH/LOW
// If you do not want to have LED support, comment the line below
//#define USE_WIFI_LED
#ifndef WIFI_LED_PIN
  #ifdef ESP32_S3
    #define WIFI_LED_PIN  48      // ESP32-S3-Dev: WS2812 on GPIO 48
  #endif
  #ifdef ESP32_C6
    #define WIFI_LED_PIN  8       // ESP32-C6-Dev: WS2812 on GPIO 8
  #endif
  #ifdef ESP32_WROOM
    #define WIFI_LED_PIN  2       // Most ESP32-WROOM have it on PIN 2
  #endif
#endif    
#ifndef WIFI_LED_RGB
  #ifdef ESP32_S3
    #define WIFI_LED_RGB  1       // 1 = WS2812; 0 = plain GPIO LED
  #endif
  #ifdef ESP32_C6
    #define WIFI_LED_RGB  1       // 1 = WS2812; 0 = plain GPIO LED
  #endif
  #ifdef ESP32_WROOM
    #define WIFI_LED_RGB  0       // 1 = WS2812; 0 = plain GPIO LED
  #endif
#endif
#ifndef WIFI_LED_ON
  #ifdef ESP32_S3
    #define WIFI_LED_ON   HIGH
  #endif
  #ifdef ESP32_C6
    #define WIFI_LED_ON   HIGH
  #endif
  #ifdef ESP32_WROOM
    #define WIFI_LED_ON   HIGH
  #endif
#endif


#endif
