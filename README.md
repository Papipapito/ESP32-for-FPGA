# ESP32-C6 for FPGA

Firmware del **companion ESP32-C6** para los cores MSX en FPGA de este proyecto:
**MSXnano** (Tang Nano 20K) y **MSXimus** (Tang Console 60K).

**Un solo código, una sola placa, las dos máquinas.**

---

## Qué hace

| | |
|---|---|
| **WiFi / UNAPI** | Pila TCP y TLS para el MSX, por UART a 859372 bps |
| **Pantalla** | Estado del sistema, arranque, reloj (240×240) |
| **Turbo** | Lee el GPIO que el FPGA sube en modo turbo y lo pinta |

Y nada más. El módulo es la **Waveshare ESP32-C6-LCD-1.3** (ST7789V2 240×240
SPI) y le unen al FPGA **tres cables**.

## El firmware NO sabe en qué máquina está

Y no tiene por qué. MSXnano y MSXimus hablan el **mismo protocolo** con el ESP,
usan los **mismos pines** y no hay ninguna función que tenga una y la otra no.
El mismo binario vale para las dos: **no se elige proyecto en ningún sitio.**

Por eso el título del LCD es neutro — `DEVICE_NAME "MSX"` en `Display.ino` — y
es el único punto de personalización que existe.

---

## Lo que se quitó, y por qué

Este repo llegó a tener **dos placas y cuatro líneas de trabajo a la vez**.
Todo eso está cerrado:

| | |
|---|---|
| **Companion ESP32-S3** | ❌ **Cancelado (26/08/2026).** Con él se van su pantalla de 320×170, la narrativa de arranque, sus bancos de prueba y el lanzador |
| **USB host** (teclado / mando / ratón) | ❌ Se va con la S3, **y no puede volver**: ver abajo |
| **Caché de descargas en FFat** | ❌ **Abandonada (04/09/2026)**, aunque llegó a validarse en placa |
| **File-Hunter en el ESP** | ❌ Abandonado con el lanzador de la S3 |
| **Cinta TSX** | ❌ **Fuera (04/09/2026)**, a la vez que su lado MSX — la tecla `T` del menú de la BIOS |

> ### ⚠️ La falta de USB host en el C6 es de SILICIO, no una decisión
>
> El ESP32-C6 sólo tiene **USB Serial/JTAG**: no es un host USB, y ningún
> cambio de software lo va a convertir en uno. Por eso el companion del MSXnano
> lleva **además** una Pico Zero RP2040 para el teclado y el mando.

Nada de esto se ha perdido: vive en la historia de git y en las ramas
`msxnano-s3`, `launcher`, `msximus` y `msxnano`.

---

## Estructura

| | |
|---|---|
| `ESP32-UNAPI-Firmware.ino` | Sketch principal: UNAPI, WiFi, bucle |
| `Display.ino` | Pantalla 240×240: estado, arranque, reloj, turbo |
| `UNAPIESP.h`, `ESP32BOARDS.h` | Órdenes propias y placas del upstream |
| `LogoMsximus.h` | Logo de arranque del LCD (lo genera `tools/make_logo_c6.py`) |
| `INVENTARIO_C6.md` | Inventario técnico del módulo y su cableado |

---

## Compilar y flashear

**Siempre por línea de órdenes**, con el `arduino-cli` que trae el propio
Arduino IDE. Nunca desde el IDE.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app .
```

⚠️ **El esquema de partición no es opcional**: los certificados TLS viven en
FFat y con otro esquema el firmware no arranca bien.

---

## Norma: TODO el código del ESP vive aquí

Nada de dejar copias dentro de los repos de los proyectos. Hubo una en
`MSX_up_v3/esp32_c6/` y en cinco semanas divergió de este repo hasta que ya no
se sabía cuál era la buena. **Un solo sitio.**

---

## Ramas

| | |
|---|---|
| **`c6`** | **La línea viva.** Sólo C6: pantalla + UNAPI + turbo |
| `unificado` | Histórico: el intento de servir a las dos placas a la vez |
| `msxnano-s3` | Histórico: companion S3 del MSXnano |
| `msximus`, `msxnano` | Histórico: ramas por proyecto |
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
| **proyecto MSXnano / MSXimus** | Pantalla de estado y turbo |

El código vive aquí, y no como un fork colgando de su repositorio, por una
razón de higiene: es el firmware de **estas** dos máquinas y evoluciona con
ellas. **Los créditos y la licencia se conservan intactos**, tanto en el
fichero `LICENSE` como en las cabeceras de cada fichero.

**Licencia: GNU LGPL v2.1** (la del proyecto original). Las partes añadidas
para MSXnano y MSXimus se publican bajo la misma licencia.

Si este firmware te resulta útil, considera apoyar a ducasp:
[ko-fi.com/R6R2BRGX6](https://ko-fi.com/R6R2BRGX6)
