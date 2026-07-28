// Stub de freertos/task.h. Firmas iguales a las de FreeRTOS/ESP-IDF.
#pragma once
#include "freertos/FreeRTOS.h"

typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char* pcName,
                       const uint32_t usStackDepth,
                       void* pvParameters,
                       unsigned uxPriority,
                       TaskHandle_t* pxCreatedTask);

void       vTaskDelete(TaskHandle_t xTask);
void       vTaskDelay(const TickType_t xTicksToDelay);
TickType_t xTaskGetTickCount(void);
