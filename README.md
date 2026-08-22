# ESP32 for FPGA

Firmware del **companion ESP32** para los cores MSX en FPGA de este proyecto:
**MSXnano** (Tang Nano 20K) y **MSXimus** (Tang Console 60K).

Un solo código, dos placas, dos máquinas.

---

## Qué hace

El ESP32 es el ayudante del MSX: se ocupa de todo lo que la FPGA no debería
tener que hacer.

| | |
|---|---|
| **WiFi / UNAPI** | Pila TCP y TLS para el MSX, por UART a 859372 bps |
| **Pantalla** | Estado del sistema, arranque, reloj |
| **Cinta TSX** | Reproducción de cinta virtual por stream, con catálogo |
| **Lanzador** | Núcleo File-Hunter para buscar y descargar |
| **HID** | Teclado, mando y ratón USB → protocolo del FPGA *(sólo S3, ver abajo)* |

---

## Dos proyectos, un binario

**El firmware NO sabe en qué máquina está, y no tiene por qué.** MSXnano y
MSXimus hablan el mismo protocolo con el ESP, así que el mismo binario vale
para las dos. No hay que elegir proyecto en ningún sitio.

Lo único que se elige al compilar es **la placa**.

---

## Dos placas, y la diferencia que importa

| | **ESP32-C6** | **ESP32-S3** |
|---|---|---|
| Módulo típico | ESP32-C6-LCD-1.3 | ESP32-1732S019 |
| Pantalla | 240×240 | 320×170 |
| **USB host** | **NO** | **SÍ** |
| Teclado / mando / ratón USB | — | Sí |

> ### ⚠️ La falta de USB host en el C6 es de SILICIO, no una decisión
>
> El ESP32-C6 sólo tiene **USB Serial/JTAG**: no es un host USB, y ningún
> cambio de software lo va a convertir en uno. Por eso, en su día, el
> companion del MSXnano necesitaba **además** una Pico Zero RP2040 sólo para
> el teclado y el mando; y por eso la placa S3, que sí tiene USB-OTG, pudo
> absorber las dos funciones ella sola.
>
> **Consecuencia práctica:** con un C6 tienes pantalla, WiFi, cinta y
> lanzador. Teclado, mando y **ratón** por ESP existen sólo en la **S3**.

### Cómo se elige

Todo en **`Board.h`**, que es el único sitio:

```c
//#define BOARD_C6      // ESP32-C6-LCD-1.3   — panel 240x240, sin USB host
#define BOARD_S3        // ESP32-1732S019     — panel 320x170, con USB host
```

De ahí se **derivan** las capacidades (`BOARD_SCREEN_C6`, `BOARD_SCREEN_S3`,
`BOARD_HAS_USB_HOST`) y los módulos se guardan **por capacidad, no por nombre
de placa**. Si mañana aparece una tercera placa, sólo hay que declarar qué
tiene. Elegir dos placas —o ninguna— da un `#error`, no un binario raro.

---

## Estructura

| | |
|---|---|
| `ESP32-UNAPI-Firmware.ino` | Sketch principal: UNAPI, WiFi, bucle |
| `Board.h` | **Selección de placa y capacidades** |
| `BoardS3.h` | Mapa de pines de la S3 |
| `Display.ino` | Pantalla del **C6** (240×240) |
| `ScreenS3.*` | Pantalla de la **S3** (320×170) |
| `UsbHost.*`, `MsxHid.*`, `XInputHost.*` | HID USB → protocolo del FPGA *(S3)* |
| `Tape.ino`, `tsxcatalog.*`, `tsx2cvs.*` | Cinta virtual TSX |
| `FileHunter.*` | Núcleo del lanzador |
| `host_test/` | Bancos que corren **en el PC**, sin placa |

### Bancos de prueba

Corren en WSL/Linux sin necesidad de hardware, y se compilan con `-Werror`:

```bash
cd host_test && bash test_filehunter.sh
```

Hay bancos para el lanzador, la pantalla de la S3, el HID, XInput, y dos de
verificación byte a byte contra una implementación de referencia en Python.

---

## Compilar

Elegir la placa en `Board.h` (línea 27-28) y compilar. Ajustes del IDE para el
**ESP32-1732S019**:

| ajuste | valor |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |

⚠️ **El esquema de partición con FATFS no es opcional**: los certificados TLS
viven en FFat y con otro esquema el firmware no arranca bien.

Desde línea de órdenes, con el `arduino-cli` que trae el propio Arduino IDE:

```bash
arduino-cli compile   --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" .
```

Referencia: 54% del espacio de programa y 32% de la RAM (core ESP32 3.3.10).

## Ramas

| | |
|---|---|
| **`unificado`** | **La línea viva.** Un firmware para las dos placas |
| `msxnano-s3` | Histórico: companion S3 del MSXnano |
| `msximus`, `msxnano` | Histórico: ramas por proyecto (C6) |
| `launcher` | Histórico: donde nació el núcleo File-Hunter |
| `main` | Espejo del upstream de ducasp |

---

## Origen y licencia

Este firmware **deriva del excelente trabajo de Oduvaldo Pavan Junior
(ducasp)**, [ESP32-UNAPI-Firmware](https://github.com/ducasp/ESP32-UNAPI-Firmware),
que es quien puso toda la pila UNAPI/WiFi que aquí se usa. Su documentación
original se conserva en [`README-UNAPI.md`](README-UNAPI.md).

Autores cuyo trabajo está dentro de este código:

| | |
|---|---|
| **Oduvaldo Pavan Junior** (ducasp) | Pila UNAPI / WiFi / TLS — la base de todo |
| **Jeroen Taverne** | Funcionalidad HTTP (`ESP32-UNAPI-Firmware.ino`, `UNAPIESP.h`) |
| **Leo Manes** | Contribuciones al firmware ESP32 |
| **proyecto MSXnano / MSXimus** | Pantallas, cinta TSX, HID USB, lanzador, unificación |

El código vive aquí, y no como un fork colgando de su repositorio, por una
razón de higiene: es el firmware de **estas** dos máquinas y evoluciona con
ellas. **Los créditos y la licencia se conservan intactos**, tanto en el
fichero `LICENSE` como en las cabeceras de cada fichero.

**Licencia: GNU LGPL v2.1** (la del proyecto original). Las partes añadidas
para MSXnano y MSXimus se publican bajo la misma licencia.

Si este firmware te resulta útil, considera apoyar a ducasp:
[ko-fi.com/R6R2BRGX6](https://ko-fi.com/R6R2BRGX6)
