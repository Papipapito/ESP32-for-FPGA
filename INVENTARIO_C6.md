# Inventario técnico de los companion ESP32-C6

**Revisado:** 19/10/2026 · **Rama viva:** `c6`

Este documento describe únicamente el hardware que admite esta rama. Los
experimentos antiguos de cinta, lanzador y USB host siguen disponibles en la
historia de Git, pero no forman parte del firmware C6 actual.

## Placas admitidas

| | C6-LCD-1.3 | C6-LCD-1.47 |
|---|---|---|
| MCU | ESP32-C6 | ESP32-C6FH4 |
| Flash | 4 MB | 4 MB |
| Controlador LCD | ST7789V2 | ST7789 |
| Resolución física | 240×240 | 172×320 |
| Orientación del firmware | 240×240, rotación 2 | 320×172, rotación 1 |
| Offset ST7789 | filas: 0/80 | columnas: 34/34 |
| Retroiluminación | GPIO22, encendida | GPIO22, PWM al 50 % |
| RGB integrado | WS2812 en GPIO8 | WS2812 en GPIO8 |
| Perfil de compilación | predeterminado | `WAVESHARE_C6_LCD_147` |

La ficha oficial de la placa nueva es
[ESP32-C6-LCD-1.47](https://docs.waveshare.com/ESP32-C6-LCD-1.47). Waveshare
recomienda no mantener su retroiluminación por encima del 50 %, por eso ese es
el valor del perfil de 1,47".

## Pinout común

### LCD

| Señal | GPIO | Observaciones |
|---|---:|---|
| MOSI | 6 | compartido con MOSI de la microSD |
| SCLK | 7 | compartido con SCLK de la microSD |
| CS | 14 | selección del LCD |
| DC | 15 | datos/órdenes |
| RST | 21 | reset del LCD |
| BL | 22 | transistor de retroiluminación, activo alto |

El perfil de 1,47" usa la geometría oficial de Arduino_GFX para el panel
ST7789 de 172×320: ancho/alto nativos `172, 320` y offsets `34, 0, 34, 0`.

### Enlace con el FPGA

| Señal | GPIO C6 | Dirección vista desde el ESP |
|---|---:|---|
| UART TX | 16 | ESP → FPGA |
| UART RX | 17 | FPGA → ESP |
| Turbo | 3 | FPGA → ESP, `INPUT_PULLDOWN` |
| Alimentación | 5V + GND | la placa regula a 3,3 V |

Las dos placas usan UART0 a 859372 bps, 8N1. `Serial` es el enlace binario
UNAPI y nunca una consola: cualquier texto de diagnóstico rompería el protocolo.

### Otros periféricos de la C6-LCD-1.47

| Periférico | Pines | Uso en este firmware |
|---|---|---|
| microSD | MISO 5, MOSI 6, SCLK 7, CS 4 | no se inicializa |
| WS2812 | GPIO8 | desactivado salvo que se habilite `USE_WIFI_LED` |
| BOOT | GPIO9 | reservado para arranque |
| USB nativo | GPIO12/13 | programación/USB Serial-JTAG |

GPIO3 queda libre en el diseño oficial de la C6-LCD-1.47 y se conserva como
entrada de turbo, igual que en la placa de 1,3".

## Perfiles de pantalla

`Display.ino` contiene un solo flujo de dibujo y dos geometrías:

- La 1,3" mantiene el layout vertical de 240×240 validado en placa.
- La 1,47" gira el panel a horizontal (320×172), conserva el logo de 224×72
  centrado y compacta uptime/temperatura en una línea.
- El logo de arranque sigue siendo no bloqueante: se anima durante 0,9 s y se
  mantiene hasta los 3 s mientras WiFi y UNAPI continúan arrancando.
- Los SSID largos se recortan con `~` para impedir que Arduino_GFX envuelva el
  texto sobre la línea de RSSI.

La orientación de la C6-LCD-1.47 puede invertirse sin tocar el código compilando
con `-DLCD_ROTATION=3` además de su perfil.

## Compilación

Las dos variantes usan:

- FQBN `esp32:esp32:esp32c6:PartitionScheme=huge_app`.
- Arduino-ESP32 3.3.10.
- GFX Library for Arduino.
- La misma tabla `partitions.csv`, con aplicación grande y FFat.
- Identidades de actualización separadas: `UN32C604` para 1,3" y `UN32C647`
  para 1,47", para que una actualización no pueda instalar una geometría de
  pantalla sobre la otra. No hay servidor OTA para este firmware: los binarios
  se publican en las releases del MSXimus y del MSXnano y se graban por USB-C.

La placa de 1,3" es el valor predeterminado. La de 1,47" se selecciona con:

```text
--build-property "compiler.cpp.extra_flags=-DWAVESHARE_C6_LCD_147"
```

`ESP32BOARDS.h` produce un error de preprocesador si se intentan seleccionar a
la vez `WAVESHARE_C6_LCD_13` y `WAVESHARE_C6_LCD_147`.

## Cableado al FPGA

| Máquina | FPGA → GPIO17 | FPGA ← GPIO16 | Turbo → GPIO3 | Alimentación |
|---|---|---|---|---|
| MSXimus (Console 60K) | J10 pin 14 (W21) | J10 pin 16 (N17) | J10 pin 18 (N13) | J10 pines 11/12 |
| MSXnano (Nano 20K) | pin 27 | pin 28 | pin 29 | 5V/GND de la placa |

Al programar por USB-C conviene desconectar los 5 V del FPGA o apagarlo. Nunca
se deben unir salidas de 3,3 V de las dos placas.

## Resultado de la revisión de ramas y repositorios

| Lugar | Estado | Decisión |
|---|---|---|
| `ESP32-for-FPGA/c6` | línea C6 activa | conserva solo C6-LCD-1.3 y C6-LCD-1.47 |
| `main` | upstream de ducasp | referencia histórica |
| `msxnano`, `msximus`, `launcher`, `unificado`, `msxnano-s3` | prototipos y líneas cerradas | no mezclar en `c6` |
| [`MSXimus_zynq/esp32`](https://github.com/Papipapito/MSXimus_zynq/tree/zynq/esp32) | firmware S3 vigente de la Zynq | dueño de ESP32-1732S019 y Waveshare ESP32-S3-LCD-1.69 |

La copia de `MSXimus_zynq` incluye funciones específicas de aquella máquina,
como la cinta web y la actualización de la Zynq. Es más reciente que el soporte
S3 que había quedado accidentalmente en esta rama C6; por tanto, el código S3
se elimina aquí y no se duplica.

## Fuera de alcance

- USB host: el ESP32-C6 solo ofrece USB Serial/JTAG y no puede actuar como host.
- Teclado, ratón y mando USB.
- Cinta TSX, File-Hunter y caché web.
- Perfiles ESP32-S3 y ESP32-WROOM.
- Consola de depuración por `Serial`.

Estas funciones no se han borrado de la historia. Solo se han retirado de la
rama de producción C6 para que cada repositorio tenga una responsabilidad clara.
