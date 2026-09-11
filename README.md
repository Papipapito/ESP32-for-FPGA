# ESP32-C6 for FPGA

Firmware del **companion ESP32-C6** para los cores MSX en FPGA de este proyecto:
**MSXnano** (Tang Nano 20K) y **MSXimus** (Tang Console 60K).

**Un solo código, una sola placa, las dos máquinas.**

---

## Qué hace

| | |
|---|---|
| **WiFi / UNAPI** | Pila TCP/IP y TLS para el MSX, por UART a 859372 bps |
| **Pantalla** | Logo de arranque y pantalla de estado: WiFi, turbo, reloj (240×240) |
| **Turbo** | Lee el GPIO que el FPGA sube en modo turbo y lo pinta |

Y nada más. El módulo es la **Waveshare ESP32-C6-LCD-1.3** (ST7789V2 240×240
SPI) y le unen al FPGA **tres cables** más la alimentación.

## El firmware NO sabe en qué máquina está

Y no tiene por qué. MSXnano y MSXimus hablan el **mismo protocolo** con el ESP,
usan los **mismos pines** y no hay ninguna función que tenga una y la otra no.
El mismo binario vale para las dos: **no se elige proyecto en ningún sitio.**

Por eso el título del LCD es neutro — `DEVICE_NAME "MSX"` en `Display.ino` — y
es el único punto de personalización que existe.

---

## Lo que sale en pantalla

### Al encender: el logo

Fondo azul MSX y el logotipo **MSX armándose desde los dos lados**, como en el
arranque de un MSX2 real: dos copias entran de lados opuestos, se cruzan, y
donde coinciden queda blanco. Tarda 0,9 s en armarse y se ve 3 s en total.
No bloquea nada: la pila WiFi arranca mientras tanto.

### Después: la pantalla de estado

```
 ┌────────────────────────────────┐
 │ MSX                          ● │  título + punto de actividad UART
 ├────────────────────────────────┤
 │ Conectado                      │  o "Sin WiFi" en rojo y
 │ SSID:mired                     │  "Pulsa W en el menu"
 │ RSSI:-58dBm                    │
 ├────────────────────────────────┤
 │ ████ TURBO 5.37MHz ████        │  naranja en turbo,
 │                                │  gris "Normal 3.58MHz" si no
 ├────────────────────────────────┤
 │ Uptime 0:12:34                 │
 │ Temp 41 C                      │  la del propio ESP
 │ 18:42  11/09/26                │  hora local por SNTP
 └────────────────────────────────┘
```

| Zona | Qué muestra | Se refresca |
|---|---|---|
| Punto arriba a la derecha | Verde si ha habido tráfico con el MSX en el último cuarto de segundo, gris si no. Es el primer diagnóstico: si no parpadea al usar la red, el problema es el cable o el core, no la WiFi | cada 150 ms |
| Bloque WiFi | *Conectado* con SSID y señal en dBm, o *Sin WiFi* con el aviso de pulsar **W** en el menú de la BIOS para configurarla | cada 1 s |
| Barra de turbo | *TURBO 5.37MHz* en naranja cuando el FPGA sube el pin de turbo; *Normal 3.58MHz* en gris si no. Sin cable el pin lee 0 y dice Normal | cada 400 ms |
| Barra inferior | Tiempo encendido, temperatura del chip ESP y la hora local. Hasta que el reloj sincroniza dice *Sincronizando*, o *Sin WiFi* | cada 3 s |

El reloj se pone en hora solo, por SNTP, en cuanto hay WiFi; el huso horario es
el que se configura desde el MSX.

---

## Qué lleva el firmware

Toda la pila de red es la de **ducasp**, intacta. Lo añadido es la pantalla y el
turbo.

| | |
|---|---|
| **TCP/IP UNAPI** | La especificación completa: TCP, UDP, RAW, DNS, ping, hasta 4 conexiones a la vez. Más las extensiones HTTP de Jeroen Taverne |
| **TLS** | Conexiones seguras con verificación contra un paquete de certificados CA que vive en la partición FFat (`certs.bin`) |
| **SSH** | Cliente SSH completo según la especificación de ducasp: terminal PTY o RAW, autenticación por contraseña o clave, `known_hosts` en FFat |
| **Configuración desde el MSX** | Buscar y unirse a redes, huso horario, reloj automático, actualizar firmware y certificados por HTTP. Es lo que abre la tecla **W** del menú de la BIOS |
| **Enlace con el FPGA** | UART0 a 859372 bps, 8N1, 3,3 V. Identificador de placa `UN32C604` |
| **Pantalla y turbo** | `Display.ino`: el logo, la pantalla de estado y la lectura del pin de turbo |

Regla de oro heredada de ducasp: **nunca imprimir texto por `Serial`**. Esa UART
es la del FPGA y cualquier byte suelto corrompe el protocolo UNAPI.

---

## Pines y cableado

### Lado del módulo (igual en las dos máquinas)

Tiras de pines de la cara trasera, la del USB-C y la microSD:

| Pin del C6 | Función | Va a |
|---|---|---|
| **IO16** (TX0) | ESP → FPGA | la entrada de recepción del FPGA |
| **IO17** (RX0) | FPGA → ESP | la salida de transmisión del FPGA |
| **GPIO3** | Turbo, entrada con pull-down | la salida de turbo del FPGA |
| **5V** | Alimentación | 5 V del FPGA (el módulo regula a 3,3 V) |
| **GND** | Masa | GND |

Serigrafía de las tiras: izquierda `23 · 20 · 17 · 16 · 13 · 12`, derecha
`3 · 2 · 1 · 3V3 · GND · 5V`. La pantalla usa GPIOs internos (SCK 7, MOSI 6,
CS 14, DC 15, RST 21, BL 22) que no salen a las tiras.

### Lado del FPGA

| Máquina | FPGA → ESP (a IO17) | FPGA ← ESP (de IO16) | Turbo (a GPIO3) | 5 V / GND |
|---|---|---|---|---|
| **MSXimus** (Console 60K) | J10 pin **14** (bola W21) | J10 pin **16** (N17) | J10 pin **18** (N13) | J10 pines **11** y **12** |
| **MSXnano** (Nano 20K) | pin **27** | pin **28** | pin **29** | 5V y GND de la placa |

En el MSXimus el J10 es el conector 2×20 libre ("SDRAM1 CONN." en el esquema
oficial). Para localizarlo sin serigrafía: el pin 12 es el único del conector
con continuidad a masa, y sus dos vecinos hacia el lado largo son el 14 y el 16.
Un cable plano de una hilera en la columna par (12-14-16-18) resuelve los tres
datos y la masa.

⚠️ El módulo aguanta tener a la vez el USB-C y los 5 V del FPGA, pero mejor no:
**al grabar por USB-C, suelta el cable de 5 V o apaga la placa.**

---

## Compilar y flashear

**Siempre por línea de órdenes**, con el `arduino-cli` que trae el propio
Arduino IDE. Nunca desde el IDE. Conecta el USB-C del módulo al PC y mira qué
puerto COM le ha tocado (en el Administrador de dispositivos sale como
*Dispositivo serie USB*).

Compilar y grabar de una vez, desde PowerShell, con el módulo en COM13:

```powershell
& "C:\Users\alber\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --upload -p COM13 --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app "C:\Users\alber\proyectosAI\msx\ESP32-UNAPI-Firmware"
```

Solo grabar lo que ya está compilado en `build/esp32.esp32.esp32c6/`:

```powershell
& "C:\Users\alber\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload -p COM13 --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app --input-dir "C:\Users\alber\proyectosAI\msx\ESP32-UNAPI-Firmware\build\esp32.esp32.esp32c6" "C:\Users\alber\proyectosAI\msx\ESP32-UNAPI-Firmware"
```

Tres cosas que cuestan compilaciones si se olvidan:

- ⚠️ **El esquema de partición `huge_app` no es opcional**: los certificados TLS
  viven en FFat y con otro esquema el firmware no cabe o no arranca bien.
- **La primera compilación tarda más de 25 minutos** (construye el core de
  Arduino entero). Las siguientes van en un par de minutos.
- En PowerShell hace falta el **`&`** delante de la ruta entrecomillada; sin él
  da *Unexpected token*.

Core de Arduino para ESP32: **3.3.10**. `ESP32BOARDS.h` ya trae `#define ESP32_C6`
y `FLASH_4M`; no hay que tocar nada.

Alternativa sin Arduino, con el binario fusionado que deja la compilación:

```powershell
esptool --chip esp32c6 --port COM13 write_flash 0x0 "C:\Users\alber\proyectosAI\msx\ESP32-UNAPI-Firmware\build\esp32.esp32.esp32c6\ESP32-UNAPI-Firmware.ino.merged.bin"
```

---

## Estructura

| | |
|---|---|
| `ESP32-UNAPI-Firmware.ino` | Sketch principal: UNAPI, WiFi, bucle |
| `Display.ino` | Pantalla 240×240: logo, estado, reloj, turbo |
| `UNAPIESP.h`, `ESP32BOARDS.h` | Órdenes propias y placas del upstream |
| `LogoMsx.h` | El logo MSX como máscara de 1 bit (lo genera `tools/make_logo_msx.py` a partir de `tools/logo_msx_src.png`) |
| `partitions.csv` | Tabla de particiones con FFat |
| `INVENTARIO_C6.md` | Inventario técnico del módulo: RTL del lado FPGA, protocolo, cinta TSX, prototipo de menú |
| `README-UNAPI.md`, `SSH UNAPI specification.md` | Documentación original de ducasp |

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
| **Oduvaldo Pavan Junior** (ducasp) | Pila UNAPI / WiFi / TLS / SSH — la base de todo |
| **Jeroen Taverne** | Funcionalidad HTTP (`ESP32-UNAPI-Firmware.ino`, `UNAPIESP.h`) |
| **Leo Manes** | Contribuciones al firmware ESP32 |
| **proyecto MSXnano / MSXimus** | Logo, pantalla de estado y turbo |

El código vive aquí, y no como un fork colgando de su repositorio, por una
razón de higiene: es el firmware de **estas** dos máquinas y evoluciona con
ellas. **Los créditos y la licencia se conservan intactos**, tanto en el
fichero `LICENSE` como en las cabeceras de cada fichero.

**Licencia: GNU LGPL v2.1** (la del proyecto original). Las partes añadidas
para MSXnano y MSXimus se publican bajo la misma licencia.

Si este firmware te resulta útil, considera apoyar a ducasp:
[ko-fi.com/R6R2BRGX6](https://ko-fi.com/R6R2BRGX6)
