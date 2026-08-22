// ============================================================================
// UsbHost.h - Teclado y mando USB del companion MSXnano (ESP32-S3).
//
// Contrato con el sketch principal. La implementacion esta en UsbHost.cpp.
//
// ---------------------------------------------------------------------------
// POR QUE ESTO ES UN .cpp Y NO UN .ino
// ---------------------------------------------------------------------------
// El IDE de Arduino genera SOLO los prototipos de las funciones que encuentra
// en los .ino, y los inserta cerca del principio del fichero. Cuando los
// #include estan dentro de un bloque condicional -como aqui, que todo el
// modulo va bajo #if CONFIG_IDF_TARGET_ESP32S3- los prototipos acaban por
// DELANTE de los includes, y ahi los tipos de la libreria todavia no existen:
//
//     error: 'EspUsbHostKeyboardState' does not name a type
//
// aunque el tipo este perfectamente definido en EspUsbHost.h. Con extension
// .cpp el IDE no toca nada y el problema desaparece de raiz. A cambio, hay que
// declarar aqui lo que el sketch principal necesita ver.
// ============================================================================
#ifndef USBHOST_H
#define USBHOST_H

// Arranca el host USB (teclado + mando) y la UART hacia el FPGA.
// Llamar UNA vez desde setup().
// Estado de lo enchufado, para que la pantalla pueda contarlo. Se leen desde
// el loop; volatiles porque los escribe el hilo del USB host.
extern volatile uint8_t g_usbHostKbdUp;   // 1 mientras hay teclado
extern volatile uint8_t g_usbHostPadUp;   // 1 mientras hay algun mando

void usbHostSetup();

// Bombea el USB y emite el resync de 250 ms que mantiene callado el watchdog
// de ~1 s del FPGA (si dejan de llegar bytes, el FPGA suelta todas las teclas).
// Llamar desde loop(); NO bloquea.
void usbHostTask();

// En placas sin USB OTG (p. ej. el ESP32-C6 viejo) las dos son stubs vacios,
// asi que el sketch principal puede llamarlas sin rodearlas de #ifdef.

#endif // USBHOST_H
