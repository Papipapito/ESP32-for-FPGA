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

You should have received a copy of the GNU Lesser General Public License
along with this file.  If not, see <https://www.gnu.org/licenses/>.
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

// Este repositorio solo genera firmware para las Waveshare ESP32-C6 de 4 MB.
// La placa de 1,3" es el perfil predeterminado. Para la nueva LCD de 1,47":
//   arduino-cli compile ... --build-property compiler.cpp.extra_flags=-DWAVESHARE_C6_LCD_147
#if defined(ESP32_S3) || defined(ESP32_WROOM) || defined(ESP32_S3_LCD169)
  #error "Esta rama solo admite ESP32-C6; el firmware S3 vive en MSXimus_zynq/esp32"
#endif
#define ESP32_C6
#define FLASH_4M
#define ESP32BAUDRATE BR859372

#if defined(WAVESHARE_C6_LCD_13) && defined(WAVESHARE_C6_LCD_147)
  #error "Selecciona solo una pantalla C6: WAVESHARE_C6_LCD_13 o WAVESHARE_C6_LCD_147"
#endif
#if !defined(WAVESHARE_C6_LCD_13) && !defined(WAVESHARE_C6_LCD_147)
  #define WAVESHARE_C6_LCD_13
#endif

// La identidad forma parte del protocolo de actualizacion. No debe compartirse
// entre binarios con distinta geometria: una OTA de la 1,3" en la 1,47" (o al
// reves) arrancaria, pero dejaria la pantalla mal configurada.
#if defined(WAVESHARE_C6_LCD_147)
  #define FIRMWARETYPE "UN32C647"  // UN32 + C6 + LCD 1,47"
#else
  #define FIRMWARETYPE "UN32C604"  // identificador historico de la C6-LCD-1.3
#endif

// ======================== ENLACE CON EL FPGA (MSX <-> ESP) ========================
// Las dos C6 usan el UART0 (Serial) en GPIO16/17 y dejan GPIO3 para turbo.
// MSXLINK conserva un nombre explicito para recordar que Serial NO es consola:
// cualquier texto de diagnostico corromperia el protocolo con el FPGA.
#define MSXLINK          Serial
#define MSXLINK_BEGIN(b) Serial.begin(b)
#define TURBO_PIN        3

// ======================== ONBOARD WIFI-CONNECTED LED ========================
// Las dos Waveshare C6 llevan un WS2812 en GPIO8. Si se quiere usar como
// indicador de WiFi, descomentar USE_WIFI_LED.
//#define USE_WIFI_LED
#ifndef WIFI_LED_PIN
  #define WIFI_LED_PIN  8
#endif
#ifndef WIFI_LED_RGB
  #define WIFI_LED_RGB  1
#endif
#ifndef WIFI_LED_ON
  #define WIFI_LED_ON   HIGH
#endif

#endif
