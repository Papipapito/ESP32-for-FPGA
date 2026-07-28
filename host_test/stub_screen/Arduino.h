// Stub MINIMO de Arduino.h para probar ScreenS3.cpp en el PC (WSL).
// Solo cubre lo que ScreenS3.cpp usa de verdad. No pretende emular nada mas.
#ifndef _STUB_ARDUINO_H
#define _STUB_ARDUINO_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

// Probamos la rama de Arduino-ESP32 3.x (LEDC direccionado por PIN).
#define ESP_ARDUINO_VERSION_MAJOR 3

// Reloj falso: lo mueve el test a voluntad.
extern uint32_t g_fakeMillis;
inline uint32_t millis() { return g_fakeMillis; }
inline void delay(uint32_t) {}

// Sensor interno del SoC. Fijo a 48 C, como el mockup del diseno.
extern float g_fakeTempC;
inline float temperatureRead() { return g_fakeTempC; }

// LEDC (backlight). Solo registramos el ultimo duty aplicado.
extern int g_blDuty;
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
inline void ledcWrite(uint8_t, uint32_t duty) { g_blDuty = (int)duty; }

#endif
