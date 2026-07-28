# Pantalla del companion MSXnano (ESP32-1732S019)

Diseño de la pantalla de estado del companion: **170x320 IPS montada en
horizontal, o sea un lienzo de 320 x 170**. Este documento existe para poder
**portarla al MSXimus** sin releer el código, y para que se entienda *por qué*
está hecha así.

El código está partido en dos a propósito, y la frontera es justo por donde hay
que cortar para el port:

| Fichero | Qué es | Al portar |
|---|---|---|
| `ScreenS3.cpp` | **El driver**: LCD, backlight, fuente, motor de celdas. Depende del hardware, no sabe nada de MSX. | Se reaprovecha cambiando pines |
| `ScreenS3Boot.cpp` | **La narrativa**: el BASIC falso, la espera, `call system`, la consola MSX-DOS. Solo escribe caracteres en la rejilla. | Se retoca o se reescribe |
| `ScreenS3_internal.h` | La costura entre ambos | Igual |
| `ScreenS3.h` | El contrato con el resto del firmware | Igual |

---

## La idea

La pantalla **imita el arranque de un MSX**: sale el BASIC, aparece el `Ok`, se
teclea solo `call system` y salta a una consola MSX-DOS que se queda fija como
panel de estado.

**Y no es teatro: es progreso real.** La fase de espera dura exactamente lo que
tarden el WiFi en conectar y el USB en enumerar. El `call system` se dispara
cuando todo está listo de verdad. Si algo falla, **no se hace el salto**: se
queda en el BASIC y lo dice ahí, en estilo MSX. La nostalgia *es* el indicador
de arranque.

## La rejilla: 40 x 21

```
320 px / 8 = 40 columnas EXACTAS  <- las mismas que el SCREEN 0 del MSX
170 px / 8 = 21,25 -> 21 filas (168 px, sobran 2 de margen)
```

Esa coincidencia de las 40 columnas es de donde cuelga toda la estética. La
última fila es la franja de teclas de función.

El motor trabaja por **celdas** (`ch` + `attr`) con doble buffer: escribir en la
rejilla es gratis (solo toca RAM) y `screenTick()` vuelca al LCD **solo las
celdas que cambiaron**, como máximo `SCREEN_CELLS_PER_TICK` (64) por llamada.
Por eso los renderizadores pueden repintarlo todo cada 250 ms sin parpadeo ni
saturar el SPI, y `screenTick()` **nunca bloquea** — importante, porque ese
mismo `loop()` atiende el enlace UNAPI con el MSX, que es lo prioritario.

El atributo de cada celda empaqueta fondo y tinta en un byte: `(fondo << 4) | tinta`.

## Paleta

| # | Color | Uso |
|---|---|---|
| 0 | `#3A31D2` | azul MSX (fondo del BASIC) |
| 1 | `#FFFFFF` | blanco (texto del BASIC) |
| 2 | `#6EC2F0` | cian de la franja de teclas |
| 3 | `#3A31D2` | azul oscuro sobre el cian |
| 4 | `#08090A` | fondo de la consola DOS |
| 5 | `#FFB53C` | fósforo ámbar |
| 6 | `#8C6321` | ámbar al 55 % (etiquetas y separadores) |

## Máquina de estados

```
ST_INIT ─► ST_BASIC_TYPE ─► ST_WAIT ─► ST_TYPE_CMD ─► ST_ENTER ─► ST_DOS
              (fase 1)      (fase 2)     (fase 3)                 (fase 4)
                                │
                                └─► ST_FAILED  (se queda en BASIC informando)
```

| Fase | Qué hace | Duración |
|---|---|---|
| 1 · `ST_BASIC_TYPE` | teclea el banner de BASIC | 14 ms/carácter + 160 ms al final de cada línea |
| 2 · `ST_WAIT` | cursor parpadeando | **la que marque el hardware real** (techo de seguridad: 45 s) |
| 3 · `ST_TYPE_CMD` | teclea `call system` | 80 ms/carácter |
| 3 · `ST_ENTER` | pausa tras Enter | 420 ms |
| 4 · `ST_DOS` | consola viva | refresco de campos cada 250 ms |

Cursor: `_` parpadeando a 500 ms de semiperiodo.

El techo de 45 s de la fase 2 **no es la duración de la espera** — es la red de
seguridad para que la pantalla no se quede colgada si un subsistema no contesta
nunca.

## Las dos pantallas

**Fase 1-3 — BASIC** (azul, texto blanco):

```
MSXnano BASIC version 2.0
Copyright 1985 by Microsoft
28815 Bytes free

Ok
call system_
```

Abajo, la franja de teclas de función en cian: hora · TURBO · WiFi · temperatura
· versión.

**Fase 4 — consola MSX-DOS** (casi negro, fósforo ámbar):

```
MSXnano v1.9        TN20K GW2AR-18
----------------------------------
WIFI  MSX-BCN            [####-]
IP    192.168.1.42
LINK  RX 1.2K/s  TX 0.4K/s
CPU   5.37MHz  TURBO ON
USB   teclado + mando   TEMP 48C
----------------------------------
A:\>_                    23:47:12
```

Los campos vivos usan **ancho fijo** (`cellField`): truncan si sobra y rellenan
con espacios si falta. Es lo que impide que al acortarse un valor quede basura
del anterior en pantalla.

**Temperatura**: la interna del **ESP32-S3** (`temperatureRead()`), no la del
FPGA. Se descartó la del Tang Nano para no tocar un bitstream que va al 89 % de
ocupación y con un 0,7 % de margen de timing; además, sin calibrar a dos puntos
el número sería inventado.

## API

```c
bool screenSetup();                 // arranca LCD + backlight
void screenTick();                  // desde loop(); NO bloquea
void screenSetWifi(ssid, ip, rssi, ok);
void screenSetUsb(kbd, pad);
void screenSetTurbo(on);
void screenSetTraffic(rxBps, txBps);
void screenSetClock(hh, mm, ss);
void screenBootFailed(motivo);      // aborta el salto: se queda en BASIC
void screenSetBacklight(percent);
bool screenBootDone();              // true cuando la consola DOS esta viva
```

Las fases 2→3 avanzan cuando el firmware ha resuelto **WiFi y USB** (llamando a
`screenSetWifi` y `screenSetUsb`), no por temporizador.

## Hardware

Pines del LCD, verificados: `CS=10 DC=11 SCLK=12 MOSI=13 RST=1 BL=14` (PWM por
LEDC, **activo alto**, arranca apagado). SPI a 24 MHz, modo 3, **offset de
columna X = 35**. Todo en `BoardS3.h`.

> ⚠️ El pinout "paralelo 8-bit" que circula por internet para esta placa es
> **falso**: es un collage de los pines del táctil GT911 de la variante C (que
> esta placa no tiene y que ni siquiera salen a los headers) con pines libres
> del header, y SCLK/MOSI renombrados WR/RD. La pantalla es SPI.

## Pruebas

`host_test/test_screens3.sh` compila el módulo en el PC contra stubs de Arduino
y `Arduino_GFX` (en `host_test/stub_screen/`), ejecuta la secuencia completa y
**vuelca las pantallas en ASCII** para poder revisar el layout sin hardware.
Comprueba además los contadores anti-parpadeo (que en reposo apenas se refresque
nada).

Lo que ese test **no** puede verificar y hay que mirar en la placa: colores
reales, legibilidad de la fuente 8x8 a tamaño real, velocidad del SPI y que el
`offset X = 35` sea el correcto para este panel.
