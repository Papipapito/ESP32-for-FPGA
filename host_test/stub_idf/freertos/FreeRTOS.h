// Stub de freertos/FreeRTOS.h. Solo tipos y macros que usa XInputHost.cpp.
#pragma once
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int      BaseType_t;

#define pdPASS              1
#define pdFALSE             0
#define pdTRUE              1
#define portMAX_DELAY       ((TickType_t)0xffffffffUL)
#define portTICK_PERIOD_MS  ((TickType_t)1)
#define tskNO_AFFINITY      0x7FFFFFFF

#define pdMS_TO_TICKS(ms)   ((TickType_t)(ms))
