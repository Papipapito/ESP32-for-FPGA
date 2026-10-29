# Firmware del ESP32: la WiFi del MSXimus Z

Firmware del **companion WiFi** de los cores MSX en FPGA de este proyecto. En el **MSXimus Z** (ZYNQ MINI) el módulo oficial es la **Waveshare ESP32-S3-LCD-1.69**, conectada al header **CAM1**; el mismo código sirve para la **Waveshare ESP32-C6-LCD-1.3** del MSXnano y del MSXimus de Tang. La placa se elige al compilar, y las dos hablan el mismo protocolo con el core.

La WiFi es opcional en la Zynq: la placa tiene su Ethernet (J3) y el procesador hace de "ESP32" por cable. Con los dos presentes manda el cable. La WiFi se usa para la red del MSX (tecla **W** del menú, File-Hunter, UNAPI) y para la **actualización por red sin cable** de la placa ([capítulo 12 del manual](../docs/manual/12-actualizar.md)). Cómo se conecta y se configura: [capítulo 07 del manual](../docs/manual/07-wifi-file-hunter.md).

---

## Qué hace

| | |
|---|---|
| **WiFi / UNAPI** | Pila TCP/IP y TLS para el MSX, por UART a 859372 bps |
| **Pantalla** | Logo de arranque y pantalla de estado: WiFi, turbo, reloj |
| **Turbo** | Lee el GPIO que el FPGA sube en modo turbo y lo pinta |
| **Cinta web** | Sirve al File-Hunter del menú el catálogo de cintas TSX de tsx.eslamejor.com (sección de abajo) |

El firmware **no sabe en qué máquina está**: todas hablan el mismo protocolo, con los mismos pines por placa, y no hay ninguna función que tenga una y otra no. Por eso el título del LCD es neutro (`DEVICE_NAME "MSX"` en `Display.ino`), el único punto de personalización.

## Las placas

| Placa | Se compila con | Pantalla | Para |
|---|---|---|---|
| **Waveshare ESP32-S3-LCD-1.69** (S3R8: 16 MB de flash y 8 MB de PSRAM) | `-DESP32_S3_LCD169` (implica `ESP32_S3`) | ST7789V2 240 × 280, USB-C nativo del S3 | **MSXimus Z** |
| Waveshare ESP32-C6-LCD-1.3 (4 MB) | nada (es la placa por defecto) | ST7789V2 240 × 240 | MSXnano y MSXimus de Tang |

---

## Lo que sale en pantalla

### Al encender: el logo

Fondo azul MSX y el logotipo **MSX armándose desde los dos lados**, como en el arranque de un MSX2 real: dos copias entran de lados opuestos, se cruzan, y donde coinciden queda blanco. Tarda 0,9 s en armarse y se ve 3 s en total. No bloquea nada: la pila WiFi arranca mientras tanto.

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

En la S3-LCD-1.69 es la misma pantalla repartida en 280 de alto y apartada de las esquinas redondeadas del panel.

| Zona | Qué muestra | Se refresca |
|---|---|---|
| Punto arriba a la derecha | Verde si ha habido tráfico con el MSX en el último cuarto de segundo, gris si no. Es el primer diagnóstico: si no parpadea al usar la red, el problema es el cable o el core, no la WiFi | cada 150 ms |
| Bloque WiFi | *Conectado* con SSID y señal en dBm, o *Sin WiFi* con el aviso de pulsar **W** en el menú de la BIOS para configurarla | cada 1 s |
| Barra de turbo | *TURBO 5.37MHz* en naranja cuando el FPGA sube el pin de turbo; *Normal 3.58MHz* en gris si no. Sin cable el pin lee 0 y dice Normal | cada 400 ms |
| Barra inferior | Tiempo encendido, temperatura del chip ESP y la hora local. Hasta que el reloj sincroniza dice *Sincronizando*, o *Sin WiFi* | cada 3 s |

El reloj se pone en hora solo, por SNTP, en cuanto hay WiFi; el huso horario es el que se configura desde el MSX.

---

## Qué lleva el firmware

Toda la pila de red es la de **ducasp**. Lo añadido es la pantalla, el turbo y la cinta web.

| | |
|---|---|
| **TCP/IP UNAPI** | La especificación completa: TCP, UDP, RAW, DNS, ping, hasta 4 conexiones a la vez. Más las extensiones HTTP de Jeroen Taverne |
| **TLS** | Conexiones seguras con verificación contra un paquete de certificados CA que vive en la partición FFat (`certs.bin`); sin él, las 16 raíces de serie de `CaRaices.h` (las de Mozilla) |
| **SSH** | Cliente SSH completo según la especificación de ducasp: terminal PTY o RAW, autenticación por contraseña o clave, `known_hosts` en FFat |
| **Configuración desde el MSX** | Buscar y unirse a redes, huso horario, reloj automático, actualizar firmware y certificados por HTTP. Es lo que abre la tecla **W** del menú de la BIOS |
| **Enlace con el FPGA** | UART a 859372 bps, 8N1, 3,3 V. Identificador de placa `UN32S316` (S3 de 16 MB) o `UN32C604` (C6 de 4 MB) |
| **Pantalla y turbo** | `Display.ino`: el logo, la pantalla de estado y la lectura del pin de turbo |
| **Cinta web** | `TsxWeb.ino` y `tsxcat.h` |

Regla de oro heredada de ducasp: **nunca imprimir texto por la UART del enlace**. Cualquier byte suelto corrompe el protocolo UNAPI. En la S3-LCD-1.69 el enlace va por `Serial1` y la consola (`Serial`, UART0) queda libre para los registros a 115200.

---

## Cableado

### ESP32-S3-LCD-1.69 a la ZYNQ MINI (header CAM1)

Se conecta con el cable SH1.0 de 12 hilos que trae la placa, numerado desde el extremo del conector que queda **lejos del USB-C** (posición 1). Se usan cuatro hilos:

| Posición | Color | Señal en la S3 | Pin del CAM1 |
|---|---|---|---|
| 1 | negro | GPIO18, turbo (entrada) | **30** |
| 2 | rojo | GPIO17, RX de la S3 | **28** |
| 3 | verde | GPIO16, TX de la S3 | **26** |
| 11 | morado | GND | **38** |

El azul (3,3 V) y el gris (5 V) se quedan **sin conectar**: la S3 se alimenta por su propio USB-C. RX y TX van cruzados, y el hilo de masa es obligatorio. El indicador de turbo (CAM1-30) es opcional. En el MSXimus de Tang (60K) los mismos hilos van a J10 18 / 14 / 16 / 12.

La pantalla usa GPIOs internos del módulo (DC 4, CS 5, SCK 6, MOSI 7, RST 8, BL 15) que no salen al cable.

### ESP32-C6-LCD-1.3 (MSXnano y MSXimus de Tang)

Tiras de pines de la cara trasera, la del USB-C y la microSD:

| Pin del C6 | Función | Va a |
|---|---|---|
| **IO16** (TX0) | ESP → FPGA | la entrada de recepción del FPGA |
| **IO17** (RX0) | FPGA → ESP | la salida de transmisión del FPGA |
| **GPIO3** | Turbo, entrada con pull-down | la salida de turbo del FPGA |
| **5V** | Alimentación | 5 V del FPGA (el módulo regula a 3,3 V) |
| **GND** | Masa | GND |

| Máquina | FPGA → ESP (a IO17) | FPGA ← ESP (de IO16) | Turbo (a GPIO3) | 5 V / GND |
|---|---|---|---|---|
| **MSXimus** (Console 60K) | J10 pin **14** (bola W21) | J10 pin **16** (N17) | J10 pin **18** (N13) | J10 pines **11** y **12** |
| **MSXnano** (Nano 20K) | pin **27** | pin **28** | pin **29** | 5V y GND de la placa |

⚠️ El módulo C6 aguanta tener a la vez el USB-C y los 5 V del FPGA, pero mejor no: **al grabar por USB-C, suelta el cable de 5 V o apaga la placa.**

---

## Compilar y flashear

**Siempre por línea de órdenes**, con el `arduino-cli` que trae el propio Arduino IDE; nunca desde el IDE. Dos manías de `arduino-cli`: la carpeta del sketch tiene que llamarse como el `.ino` (`ESP32-UNAPI-Firmware`, no `esp32`: copia o enlaza la carpeta con ese nombre) y el `partitions.csv` del sketch manda sobre el esquema de partición del FQBN. Core de Arduino para ESP32: **3.3.10**.

### Waveshare ESP32-S3-LCD-1.69 (la de la Zynq)

El USB-C de la placa es el USB nativo del S3: en el PC sale como *Dispositivo serie USB* (VID 303A). Con el módulo en COM5, desde PowerShell:

```powershell
& "C:\Users\alber\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --upload -p COM5 --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,UploadMode=default,FlashMode=qio,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" --build-property "compiler.cpp.extra_flags=-DESP32_S3 -DESP32_S3_LCD169" "C:\ruta\ESP32-UNAPI-Firmware"
```

### Waveshare ESP32-C6-LCD-1.3

`ESP32BOARDS.h` ya trae `#define ESP32_C6` y `FLASH_4M`; no hay que tocar nada. Con el módulo en COM13:

```powershell
& "C:\Users\alber\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --upload -p COM13 --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app "C:\ruta\ESP32-UNAPI-Firmware"
```

Solo grabar lo que ya está compilado en `build/esp32.esp32.esp32c6/`:

```powershell
& "C:\Users\alber\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload -p COM13 --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app --input-dir "C:\ruta\ESP32-UNAPI-Firmware\build\esp32.esp32.esp32c6" "C:\ruta\ESP32-UNAPI-Firmware"
```

Alternativa sin Arduino, con el binario fusionado que deja la compilación:

```powershell
esptool --chip esp32c6 --port COM13 write_flash 0x0 "C:\ruta\ESP32-UNAPI-Firmware\build\esp32.esp32.esp32c6\ESP32-UNAPI-Firmware.ino.merged.bin"
```

Tres cosas que cuestan compilaciones si se olvidan:

- ⚠️ **El esquema de partición no es opcional**: `huge_app` en el C6 y `app3M_fat9M_16MB` en la S3. Los certificados TLS viven en FFat y con otro esquema el firmware no cabe o no arranca bien.
- **La primera compilación tarda más de 25 minutos** (construye el core de Arduino entero). Las siguientes van en un par de minutos.
- En PowerShell hace falta el **`&`** delante de la ruta entrecomillada; sin él da *Unexpected token*.

### Cinta web (`TsxWeb.ino`)

El File-Hunter del menú pide también cintas TSX de tsx.eslamejor.com, que no tiene API: las sirve el propio ESP. El menú resuelve `tsx.msximus`, el DNS del UNAPI lo contesta con `127.0.0.1` y el `TCP_OPEN` a esa IP crea una conexión virtual (una subclase de `WiFiClient` sin socket) que responde como `MSXnano.php`: la lista en el formato del File-Hunter y la descarga del `.tsx`, que un hilo baja por HTTPS a la PSRAM mientras el menú la va leyendo. El catálogo va en flash (`tsxcat.h`, unas 2500 cintas con tamaño, CRC y orden de carga) y lo genera en el PC `tools/tsxcat_gen.py` a partir de la web; para actualizarlo: `python esp32/tools/tsxcat_gen.py --out esp32/tsxcat.h` y recompilar.

---

## Estructura

| | |
|---|---|
| `ESP32-UNAPI-Firmware.ino` | Sketch principal: UNAPI, WiFi, bucle |
| `Display.ino` | Pantalla: logo, estado, reloj, turbo (una geometría por placa) |
| `TsxWeb.ino`, `tsxcat.h` | Cinta web y su catálogo |
| `UNAPIESP.h`, `ESP32BOARDS.h` | Órdenes propias y placas: pines del enlace, del turbo y tamaño de flash |
| `CaRaices.h` | Raíces CA de serie para el TLS con validación |
| `LogoMsx.h` | El logo MSX como máscara de 1 bit (lo genera `tools/make_logo_msx.py` a partir de `tools/logo_msx_src.png`) |
| `partitions.csv` | Tabla de particiones con FFat |
| `tools/` | `make_logo_msx.py`, `tsxcat_gen.py` y el sketch de diagnóstico por pasos `diag_esp32/` |
| `INVENTARIO_C6.md` | Inventario técnico del módulo C6: RTL del lado FPGA, protocolo, cinta TSX, prototipo de menú |
| `documentation/`, `README-UNAPI.md`, `SSH UNAPI specification.md` | Documentación original de ducasp |

## Norma: todo el código del ESP vive aquí

Nada de dejar copias dentro de los repos de los proyectos: con el tiempo divergen hasta que no se sabe cuál es la buena. **Un solo sitio.**

---

## Origen y licencia

Este firmware **deriva del excelente trabajo de Oduvaldo Pavan Junior (ducasp)**, [ESP32-UNAPI-Firmware](https://github.com/ducasp/ESP32-UNAPI-Firmware), que es quien puso toda la pila UNAPI/WiFi que aquí se usa. Su documentación original se conserva en [`README-UNAPI.md`](README-UNAPI.md).

Autores cuyo trabajo está dentro de este código:

| | |
|---|---|
| **Oduvaldo Pavan Junior** (ducasp) | Pila UNAPI / WiFi / TLS / SSH: la base de todo |
| **Jeroen Taverne** | Funcionalidad HTTP (`ESP32-UNAPI-Firmware.ino`, `UNAPIESP.h`) |
| **Leo Manes** | Contribuciones al firmware ESP32 |
| **proyecto MSXnano / MSXimus** | Logo, pantalla de estado, turbo y cinta web |

El código vive aquí, y no como un fork colgando de su repositorio, por una razón de higiene: es el firmware de **estas** máquinas y evoluciona con ellas. **Los créditos y la licencia se conservan intactos**, tanto en el fichero `LICENSE` como en las cabeceras de cada fichero.

**Licencia: GNU LGPL v2.1** (la del proyecto original). Las partes añadidas para MSXnano y MSXimus se publican bajo la misma licencia.

Si este firmware te resulta útil, considera apoyar a ducasp: [ko-fi.com/R6R2BRGX6](https://ko-fi.com/R6R2BRGX6)
