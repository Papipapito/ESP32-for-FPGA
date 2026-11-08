# ESP32 for FPGA

Firmware del **companion ESP32** para los cores MSX en FPGA de este proyecto:
**MSXnano** (Tang Nano 20K), **MSXimus** (Tang Console 60K / 138K) con una
**ESP32-C6**, y **MSXimus Z** (ZYNQ MINI) con una **ESP32-S3**.

**Un solo código, tres placas, todas las máquinas.** La placa se elige al compilar.

---

## Qué hace

| | |
|---|---|
| **WiFi / UNAPI** | Pila TCP/IP y TLS para el MSX, por UART a 859372 bps |
| **Pantalla** | Logo de arranque y pantalla de estado: WiFi, turbo y reloj (240×240, 320×172 o 280×240) |
| **Turbo** | Lee el GPIO que el FPGA sube en modo turbo y lo pinta |
| **Cinta web** | Solo en la S3 (MSXimus Z): sirve al File-Hunter del menú el catálogo de cintas TSX de tsx.eslamejor.com (sección de abajo) |

Y nada más. Módulos compatibles, todos con ST7789 SPI:

| Perfil | Flash | Panel | Selección | Para |
|---|---|---|---|---|
| **Waveshare ESP32-C6-LCD-1.3** | 4 MB | 240×240 | predeterminado | MSXnano, MSXimus Tang |
| **Waveshare ESP32-C6-LCD-1.47** | 4 MB | 172×320, usada en horizontal (320×172) | `-DWAVESHARE_C6_LCD_147` | MSXnano, MSXimus Tang |
| **Waveshare ESP32-S3-LCD-1.69** (S3R8: 8 MB de PSRAM) | 16 MB | 240×280, usada en horizontal (280×240) | `-DESP32_S3_LCD169` (implica `ESP32_S3`) | **MSXimus Z** |
| ESP32-1732S019 (S3 + LCD 1.9″, **sin uso**: brownout con la WiFi) | 16 MB | 170×320, horizontal (320×170) | `-DESP32_S3` | — |

Las C6 se conectan al FPGA con **tres cables** más alimentación y usan el mismo
pinout de enlace; la S3 usa el UART1 y otros pines (ver el cableado). Solo cambia
el binario por la geometría del LCD y los pines.

## El firmware NO sabe en qué máquina está

Y no tiene por qué. MSXnano y MSXimus hablan el **mismo protocolo** con el ESP,
usan los **mismos pines** (las C6) y no hay ninguna función que tenga una y la otra no.
El mismo binario C6 vale para MSXnano y MSXimus Tang: **no se elige proyecto en ningún
sitio**. Sí hay que compilar el perfil de pantalla que corresponda a la placa.
La S3 de la Zynq habla el mismo protocolo; solo cambian los pines, la pantalla y la
cinta web, todo detrás de `ESP32_S3` / `ESP32_S3_LCD169` (`ESP32BOARDS.h`, `Display.ino`):
**compilar una C6 no incluye nada de lo de la S3.**

Por eso el título del LCD es neutro — `DEVICE_NAME "MSX"` en `Display.ino` — y
es el único punto de personalización que existe.

---

## Lo que sale en pantalla

### Al encender: el logo

Fondo azul MSX y el logotipo **MSX armándose desde los dos lados**, como en el
arranque de un MSX2 real: dos copias entran de lados opuestos, se cruzan, y
donde coinciden queda blanco. Tarda 0,9 s en armarse y se ve 3 s en total.
No bloquea nada: la pila WiFi arranca mientras tanto.

El mismo logo se centra sin deformarse en ambos paneles. En la 1,47" cabe entero
porque el firmware usa la pantalla en horizontal.

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

En la 1,47" la zona inferior se compacta en dos líneas para aprovechar sus 172
píxeles de alto: tiempo/temperatura en una y reloj/fecha en otra.

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
| **Enlace con el FPGA** | UART a 859372 bps, 8N1, 3,3 V. Identificador `UN32C604` (C6 1,3″), `UN32C647` (C6 1,47″) o `UN32S316` (S3) |
| **Pantalla y turbo** | `Display.ino`: el logo, la pantalla de estado y la lectura del pin de turbo |
| **Cinta web** (solo S3) | `TsxWeb.ino` y `tsxcat.h`; ver más abajo |

Regla de oro heredada de ducasp: **nunca imprimir texto por `Serial`** en las C6. Esa UART
es la del FPGA y cualquier byte suelto corrompe el protocolo UNAPI. En la S3 el enlace va
por `Serial1` y la consola (`Serial`, UART0) queda libre para los registros a 115200
(`HAVE_CONSOLE`).

---

## Pines y cableado

### Lado del módulo (igual en las dos C6 y en las dos máquinas)

Tiras de pines de la cara trasera, la del USB-C y la microSD:

| Pin del C6 | Función | Va a |
|---|---|---|
| **IO16** (TX0) | ESP → FPGA | la entrada de recepción del FPGA |
| **IO17** (RX0) | FPGA → ESP | la salida de transmisión del FPGA |
| **GPIO3** | Turbo, entrada con pull-down | la salida de turbo del FPGA |
| **5V** | Alimentación | 5 V del FPGA (el módulo regula a 3,3 V) |
| **GND** | Masa | GND |

En la placa de 1,3", la serigrafía de las tiras es: izquierda
`23 · 20 · 17 · 16 · 13 · 12`, derecha `3 · 2 · 1 · 3V3 · GND · 5V`. En la
1,47", TXD/RXD (GPIO16/17) y GPIO3 también salen al conector.

El LCD usa los mismos pines en las dos placas C6: SCK 7, MOSI 6, CS 14, DC 15,
RST 21 y BL 22. La microSD de la 1,47" comparte SCK/MOSI y añade MISO 5 y CS 4;
este firmware no la usa. El WS2812 de ambas placas está en GPIO8.

### ESP32-S3-LCD-1.69 a la ZYNQ MINI (header CAM1)

Se conecta con el cable SH1.0 de 12 hilos que trae la placa, numerado desde el
extremo del conector que queda **lejos del USB-C** (posición 1). Se usan cuatro hilos:

| Posición | Color | Señal en la S3 | Pin del CAM1 |
|---|---|---|---|
| 1 | negro | GPIO18, turbo (entrada) | **30** |
| 2 | rojo | GPIO17, RX de la S3 | **28** |
| 3 | verde | GPIO16, TX de la S3 | **26** |
| 11 | morado | GND | **38** |

El azul (3,3 V) y el gris (5 V) se quedan **sin conectar**: la S3 se alimenta por
su propio USB-C. RX y TX van cruzados, y el hilo de masa es obligatorio. El
indicador de turbo (CAM1-30) es opcional. En el MSXimus de Tang (60K) los mismos
hilos van a J10 18 / 14 / 16 / 12. La pantalla usa GPIOs internos del módulo
(DC 4, CS 5, SCK 6, MOSI 7, RST 8, BL 15) que no salen al cable.

### Lado del FPGA (C6)

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
*Dispositivo serie USB*). `arduino-cli` exige que la carpeta del sketch se llame
como el `.ino` (`ESP32-UNAPI-Firmware`) y el `partitions.csv` del sketch manda sobre el
esquema de partición del FQBN.

### Waveshare ESP32-S3-LCD-1.69 (la de la MSXimus Z)

El USB-C de la placa es el USB nativo del S3 (VID 303A). Compilar y grabar de una
vez, con el módulo en COM5:

```powershell
& arduino-cli compile --upload -p COM5 --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,UploadMode=default,FlashMode=qio,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" --build-property "compiler.cpp.extra_flags=-DESP32_S3_LCD169" "C:\ruta\ESP32-UNAPI-Firmware"
```

La pantalla va **apaisada** (280×240) y con el USB-C a la derecha (`LCD_ROTATION 3`,
`Display.ino`; para girarla 180° se compila con `-DLCD_ROTATION=1`). Las esquinas del
panel son redondeadas y no hay nada pegado a ellas.

⚠️ **Para actualizar una S3 que ya está en la caja se graba SOLO la app, a `0x10000`**
(`ESP32-UNAPI-Firmware.ino.bin`), nunca el `merged.bin` a `0x0`: el merged lleva el
bootloader y las particiones, y pisa la FFat con los certificados y la configuración:

```powershell
esptool --chip esp32s3 --port COM5 write_flash 0x10000 "C:\ruta\ESP32-UNAPI-Firmware.ino.bin"
```

### Waveshare ESP32-C6 (MSXnano y MSXimus Tang)

Compilar y grabar de una vez, desde PowerShell, con el módulo en COM13:

```powershell
& "C:\Users\alber\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --upload -p COM13 --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app "C:\Users\alber\proyectosAI\msx\ESP32-UNAPI-Firmware"
```

Ese comando genera el firmware de la **C6-LCD-1.3**. Para la nueva
**C6-LCD-1.47**, se añade el perfil de pantalla:

```powershell
& "C:\Users\alber\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --upload -p COM13 --fqbn esp32:esp32:esp32c6:PartitionScheme=huge_app --build-property "compiler.cpp.extra_flags=-DWAVESHARE_C6_LCD_147" "C:\Users\alber\proyectosAI\msx\ESP32-UNAPI-Firmware"
```

La orientación predeterminada de la 1,47" deja el USB-C a la derecha. Si se
monta girada, usar
`compiler.cpp.extra_flags=-DWAVESHARE_C6_LCD_147 -DLCD_ROTATION=3`.

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

Core de Arduino para ESP32: **3.3.10**. `ESP32BOARDS.h` fija C6, 4 MB y UART0
(o S3, 16 MB y UART1 con `-DESP32_S3_LCD169`); no hay que editar el fichero para cambiar de LCD. Cada perfil tiene identidad
propia para impedir actualizaciones cruzadas: `UN32C604` para 1,3″ y
`UN32C647` para 1,47″. No hay servidor OTA para este firmware: los binarios
fusionados de las dos placas van en las releases del MSXimus y del MSXnano
(`firmware_esp32c6_*_merged.bin`) y se graban por el USB-C del módulo. En la
1,47" la retroiluminación queda justo por debajo del
50 %, el máximo recomendado por Waveshare para uso continuo.

Alternativa sin Arduino, con el binario fusionado que deja la compilación:

```powershell
esptool --chip esp32c6 --port COM13 write_flash 0x0 "C:\Users\alber\proyectosAI\msx\ESP32-UNAPI-Firmware\build\esp32.esp32.esp32c6\ESP32-UNAPI-Firmware.ino.merged.bin"
```

---

## Estructura

| | |
|---|---|
| `ESP32-UNAPI-Firmware.ino` | Sketch principal: UNAPI, WiFi, bucle |
| `Display.ino` | Perfiles 240×240, 320×172 (C6) y 280×240 (S3): logo, estado, reloj y turbo |
| `TsxWeb.ino`, `tsxcat.h` | Cinta web y su catálogo (solo S3; en las C6 no compilan nada) |
| `UNAPIESP.h`, `ESP32BOARDS.h` | Órdenes propias, selección de placa/LCD y pines |
| `CaRaices.h` | Raíces CA de serie para el TLS con validación (`tools/make_ca_raices.py`) |
| `LogoMsx.h` | El logo MSX como máscara de 1 bit (lo genera `tools/make_logo_msx.py` a partir de `tools/logo_msx_src.png`) |
| `partitions.csv` | Tabla de particiones con FFat |
| `INVENTARIO_C6.md` | Matriz técnica de las dos C6, pines y perfiles de compilación |
| `tools/` | `make_logo_msx.py`, `tsxcat_gen.py` y el sketch de diagnóstico por pasos del S3, `diag_esp32/` |
| `README-UNAPI.md`, `SSH UNAPI specification.md` | Documentación original de ducasp |

---

## Cinta web (solo S3, `TsxWeb.ino`)

El File-Hunter del menú de la MSXimus Z pide también cintas TSX de tsx.eslamejor.com,
que no tiene API: las sirve el propio ESP. El menú resuelve `tsx.msximus`, el DNS del
UNAPI lo contesta con `127.0.0.1` y el `TCP_OPEN` a esa IP crea una conexión virtual
(una subclase de `WiFiClient` sin socket) que responde como `MSXnano.php`: la lista en
el formato del File-Hunter y la descarga del `.tsx`, que un hilo baja por HTTPS a la
PSRAM mientras el menú la va leyendo. El catálogo va en flash (`tsxcat.h`, unas 2500
cintas con tamaño, CRC y orden de carga) y lo genera en el PC `tools/tsxcat_gen.py`
a partir de la web; para actualizarlo: `python tools/tsxcat_gen.py` y recompilar.

---

## Lo que se quitó, y por qué

Este repo llegó a tener **dos placas y cuatro líneas de trabajo a la vez**.
Todo eso está cerrado:

| | |
|---|---|
| **Companion ESP32-S3 del MSXnano / lanzador** | ❌ Cancelado (26/08/2026). El S3 que hay ahora es otro: el de la **MSXimus Z** (Waveshare ESP32-S3-LCD-1.69), que se trajo aquí desde el repo de la Zynq (08/11/2026) |
| **USB host** (teclado / mando / ratón) | ❌ Se va con la S3, **y no puede volver**: ver abajo |
| **Caché de descargas en FFat** | ❌ **Abandonada (04/09/2026)**, aunque llegó a validarse en placa |
| **File-Hunter en el ESP** | ❌ Abandonado con el lanzador de la S3 |
| **Cinta TSX** | ❌ **Fuera (04/09/2026)**, a la vez que su lado MSX — la tecla `T` del menú de la BIOS |

> ### ⚠️ La falta de USB host en el C6 es de SILICIO, no una decisión
>
> El ESP32-C6 sólo tiene **USB Serial/JTAG**: no es un host USB, y ningún
> cambio de software lo va a convertir en uno. Por eso el companion del MSXnano
> lleva **además** una Pico Zero RP2040 para el teclado y el mando.

Nada de esto se ha perdido: los prototipos anteriores viven en la historia y en
las ramas `msxnano-s3`, `launcher`, `msximus` y `msxnano`.

---

## Norma: todo el código del ESP vive aquí

El firmware de las **C6** y el companion **S3 de la Zynq** viven aquí y solo aquí
(rama `c6`). Nada de dejar copias dentro de los repos de los proyectos (`MSXimus_zynq`,
`MSX_up_v3`, `MSXnano`...). Ya hubo una copia del C6 en `MSX_up_v3/esp32_c6/` y
otra del S3 en `MSXimus_zynq/esp32/`, y divergieron hasta que dejó de estar claro
cuál era la buena. **Un firmware, un repositorio propietario.**

---

## Ramas

| | |
|---|---|
| **`c6`** | **La línea viva.** Waveshare C6-LCD-1.3, C6-LCD-1.47 y S3-LCD-1.69: pantalla + UNAPI + turbo (+ cinta web en la S3) |
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
| **proyecto MSXnano / MSXimus** | Logo, pantalla de estado, turbo y cinta web |

El código vive aquí, y no como un fork colgando de su repositorio, por una
razón de higiene: es el firmware de **estas** dos máquinas y evoluciona con
ellas. **Los créditos y la licencia se conservan intactos**, tanto en el
fichero `LICENSE` como en las cabeceras de cada fichero.

**Licencia: GNU LGPL v2.1** (la del proyecto original). Las partes añadidas
para MSXnano y MSXimus se publican bajo la misma licencia.

Si este firmware te resulta útil, considera apoyar a ducasp:
[ko-fi.com/R6R2BRGX6](https://ko-fi.com/R6R2BRGX6)
