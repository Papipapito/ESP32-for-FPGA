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

// Este repositorio genera el firmware del companion ESP32 de los MSX en FPGA.
// Sin defines, o con -DWAVESHARE_C6_LCD_147, es una Waveshare ESP32-C6 de 4 MB (MSXnano y MSXimus 60K/138K):
// la placa de 1,3" es el perfil predeterminado. Para la nueva LCD de 1,47":
//   arduino-cli compile ... --build-property compiler.cpp.extra_flags=-DWAVESHARE_C6_LCD_147
// El companion de la Zynq (MSXimus Z) es un S3 y se elige con -DESP32_S3_LCD169 (Waveshare ESP32-S3-LCD-1.69,
// S3R8: 16 MB + 8 MB PSRAM, ST7789V2 240x280, USB-C nativo, conector SH1.0 de 12 pines) o, la variante antigua
// ya sin uso, -DESP32_S3 (ESP32-1732S019, S3 + LCD 1.9" 170x320). Un S3 solo cambia los pines (aqui y en Display.ino),
// la pantalla y la cinta web (TsxWeb.ino); el protocolo con el MSX es el mismo.
// ESP32_S3_LCD169 implica ESP32_S3.
#ifdef ESP32_S3_LCD169
  #ifndef ESP32_S3
    #define ESP32_S3
  #endif
#endif
#if defined(ESP32_WROOM)
  #error "ESP32-WROOM no esta soportado en este repositorio (solo C6 y S3)"
#endif
#ifdef ESP32_S3
  #define HAVE_TSXWEB 1       // cinta web de tsx.eslamejor (TsxWeb.ino + tsxcat.h): solo la Zynq, las C6 no la llevan
  #define FLASH_16M           // ESP32-1732S019 = modulo N16R8; S3-LCD-1.69 = S3R8 + W25Q128 (16 MB las dos)
#else
  #define ESP32_C6
  #define FLASH_4M
#endif
#define ESP32BAUDRATE BR859372

#if defined(ESP32_S3) && (defined(WAVESHARE_C6_LCD_13) || defined(WAVESHARE_C6_LCD_147))
  #error "WAVESHARE_C6_LCD_* es de las C6: no se mezcla con -DESP32_S3 / -DESP32_S3_LCD169"
#endif
#if defined(WAVESHARE_C6_LCD_13) && defined(WAVESHARE_C6_LCD_147)
  #error "Selecciona solo una pantalla C6: WAVESHARE_C6_LCD_13 o WAVESHARE_C6_LCD_147"
#endif
#if !defined(ESP32_S3) && !defined(WAVESHARE_C6_LCD_13) && !defined(WAVESHARE_C6_LCD_147)
  #define WAVESHARE_C6_LCD_13
#endif

// La identidad forma parte del protocolo de actualizacion. No debe compartirse
// entre binarios con distinta geometria: una OTA de la 1,3" en la 1,47" (o al
// reves) arrancaria, pero dejaria la pantalla mal configurada.
#if defined(ESP32_S3)
  #define FIRMWARETYPE "UN32S316"  // UN32 + S3 + 16 MB (el companion de la Zynq)
#elif defined(WAVESHARE_C6_LCD_147)
  #define FIRMWARETYPE "UN32C647"  // UN32 + C6 + LCD 1,47"
#else
  #define FIRMWARETYPE "UN32C604"  // identificador historico de la C6-LCD-1.3
#endif

// ======================== ENLACE CON EL FPGA (MSX <-> ESP) ========================
// Las dos C6 usan el UART0 (Serial) en GPIO16/17 y dejan GPIO3 para turbo.
// MSXLINK conserva un nombre explicito para recordar que Serial NO es consola:
// cualquier texto de diagnostico corromperia el protocolo con el FPGA.
// En un S3 el UART0 no vale (en la ESP32-1732S019 es del CH340 del USB-C): el enlace va por el UART1 y el Serial
// queda libre como consola de depuracion (HAVE_CONSOLE, 115200).
//   ESP32-1732S019 (header P2, de arriba abajo: 2 42 41 40 39 38 45 48 47 21 GND 5V)
//   ZYNQ MINI (CAM1)         ESP32-1732S019
//   26 W16 esp_rx_i   <----  GPIO40 (TX del ESP)
//   28 R18 esp_tx_o   ---->  GPIO39 (RX del ESP)
//   30 P19 esp_turbo  ---->  GPIO41 (entrada, pull-down)
//   37/38 GND         -----  GND (P2, penultimo por abajo)
//   Alimentacion: el USB-C de la S3 (a un USB-A de la Zynq o a un cargador). NUNCA unir los 3V3.
// Waveshare ESP32-S3-LCD-1.69: el enlace va por el cable SH1.0 de 12 hilos que trae la placa (posicion 1 = el extremo
// del conector lejos del USB-C; colores del dibujo de Waveshare). Esquema ESP32-S3-LCD-1.69 V2, conector P2.
//   ZYNQ MINI (CAM1)  60K (J10)              S3-LCD-1.69 (cable)
//   26 esp_rx_i       16 RX del FPGA  <----  pos 3 verde   GPIO16 (TX del ESP)
//   28 esp_tx_o       14 TX del FPGA  ---->  pos 2 rojo    GPIO17 (RX del ESP)
//   30 esp_turbo      18 TURBO        ---->  pos 1 negro   GPIO18 (entrada, pull-down)
//   37/38 GND         12 GND          -----  pos 11 morado GND
//   Sin conectar: pos 4-10 y sobre todo 10 azul (3,3 V, salida del regulador) y 12 gris (5 V = VBUS del USB-C).
//   Alimentacion: SU USB-C (USB-A de la Zynq o cargador), una sola fuente. GPIO3 (pos 4) es de arranque: no se usa.
#ifdef ESP32_S3_LCD169
  #define MSXLINK          Serial1
  #define MSXLINK_RX_PIN   17
  #define MSXLINK_TX_PIN   16
  #define MSXLINK_BEGIN(b) Serial1.begin((b), SERIAL_8N1, MSXLINK_RX_PIN, MSXLINK_TX_PIN)
  #define TURBO_PIN        18
  #define HAVE_CONSOLE     1     // Serial = UART0 (pos 8 naranja TX, pos 9 verde RX del cable): logs a 115200 si se cablean
#elif defined(ESP32_S3)
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
// Las dos Waveshare C6 llevan un WS2812 en GPIO8 (el S3 de desarrollo, en GPIO48). Si se quiere usar como
// indicador de WiFi, descomentar USE_WIFI_LED.
//#define USE_WIFI_LED
#ifndef WIFI_LED_PIN
  #ifdef ESP32_S3
    #define WIFI_LED_PIN  48
  #else
    #define WIFI_LED_PIN  8
  #endif
#endif
#ifndef WIFI_LED_RGB
  #define WIFI_LED_RGB  1
#endif
#ifndef WIFI_LED_ON
  #define WIFI_LED_ON   HIGH
#endif

#endif
