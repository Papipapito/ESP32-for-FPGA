// Stub de esp_log.h. Los ESP_LOGx se mandan a printf A PROPOSITO: asi el
// compilador comprueba las cadenas de formato igual que en la placa (esp_log
// lleva __attribute__((format(printf)))). Un %s con un entero se caza aqui.
#pragma once
#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) ((void)(tag), (void)printf(fmt "\n", ##__VA_ARGS__))
#define ESP_LOGW(tag, fmt, ...) ((void)(tag), (void)printf(fmt "\n", ##__VA_ARGS__))
#define ESP_LOGI(tag, fmt, ...) ((void)(tag), (void)printf(fmt "\n", ##__VA_ARGS__))
#define ESP_LOGD(tag, fmt, ...) ((void)(tag), (void)printf(fmt "\n", ##__VA_ARGS__))
#define ESP_LOGV(tag, fmt, ...) ((void)(tag), (void)printf(fmt "\n", ##__VA_ARGS__))
