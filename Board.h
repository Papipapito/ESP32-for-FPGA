// ===========================================================================
//  Board.h — EL UNICO SITIO donde se elige la placa del companion.
//
//  Este firmware es COMUN a los dos proyectos: MSXnano y MSXimus. El proyecto
//  NO se elige aqui ni en ningun sitio — el mismo binario vale para los dos,
//  porque hablan el mismo protocolo con la FPGA. Lo unico que cambia entre
//  compilaciones es LA PLACA.
//
//  Dos ejes, y solo dos:
//    - PLACA:   C6 (pantalla pequena, SIN USB host) o S3 (pantalla grande, CON
//               USB host). Se elige abajo.
//    - COMUN:   WiFi/UNAPI, cinta TSX, lanzador File-Hunter. Igual en las dos.
//
//  ⚠️ LA FALTA DE USB HOST EN EL C6 ES DE SILICIO, NO UNA DECISION.
//  El ESP32-C6 solo tiene USB Serial/JTAG; no es un host USB. Por eso en su
//  dia el companion del MSXnano necesitaba ADEMAS una Pico Zero RP2040 para el
//  teclado y el joystick, y por eso la placa S3 (que si tiene USB-OTG) pudo
//  absorber las dos funciones ella sola. Consecuencia practica: teclado,
//  joystick y raton por ESP SOLO existen en la S3.
// ===========================================================================
#ifndef BOARD_H
#define BOARD_H

// --------------------------------------------------------------------------
//  ELIGE UNA SOLA PLACA
// --------------------------------------------------------------------------
//#define BOARD_C6      // ESP32-C6-LCD-1.3   — panel 240x240, sin USB host
#define BOARD_S3        // ESP32-1732S019     — panel 320x170, con USB host

// --------------------------------------------------------------------------
//  CAPACIDADES DERIVADAS — no se editan a mano, salen de la placa elegida.
//  Los modulos se guardan con ESTAS, no con el nombre de la placa: asi, si
//  manana aparece una tercera placa, solo hay que declarar que tiene.
// --------------------------------------------------------------------------
#if defined(BOARD_C6) && defined(BOARD_S3)
  #error "Board.h: elige UNA sola placa (BOARD_C6 o BOARD_S3), no las dos."
#endif
#if !defined(BOARD_C6) && !defined(BOARD_S3)
  #error "Board.h: no has elegido placa (BOARD_C6 o BOARD_S3)."
#endif

#ifdef BOARD_S3
  #define BOARD_HAS_USB_HOST  1   // USB-OTG: teclado, joystick y raton
  #define BOARD_SCREEN_S3     1   // ScreenS3.cpp — panel 320x170
  #include "BoardS3.h"            // mapa de pines de la S3
#endif

#ifdef BOARD_C6
  #define BOARD_SCREEN_C6     1   // Display.ino — panel 240x240
  // Sin BOARD_HAS_USB_HOST a proposito: el C6 no puede ser host USB.
#endif

// --------------------------------------------------------------------------
//  EL ENLACE CON EL MSX (UART0 = "Serial")
// --------------------------------------------------------------------------
//  Todo el codigo UNAPI habla por "Serial". En el C6 eso ya sale por el cable
//  al FPGA, pero EN LA S3 EL UART0 VA AL CH340 DEL USB-C: sin remapear, el
//  enlace con el MSX se iria por el puerto de depuracion y no habria ni un
//  sintoma que apuntara al sitio.
//
//  Se remapea en tiempo de EJECUCION y se sigue flasheando con normalidad: el
//  bootloader usa 43/44 antes de que corra nuestro codigo. Lo unico que se
//  pierde es la consola serie, que en esta placa va a la PANTALLA.
#ifdef BOARD_S3
  #define MSX_LINK_BEGIN(baud)       Serial.begin((baud), SERIAL_8N1, S3_FPGA_UNAPI_RX, S3_FPGA_UNAPI_TX)
#else
  #define MSX_LINK_BEGIN(baud)  Serial.begin(baud)
#endif

#endif // BOARD_H
