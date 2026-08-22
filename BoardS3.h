// ============================================================================
// BoardS3.h - Mapa de pines UNICO del companion MSXnano sobre ESP32-1732S019.
//
// Placa: ESP32-1732S019N (Shenzhen Jingcal) = modulo ESP32-S3-WROOM-1 N16R8
//        (16 MB flash + 8 MB PSRAM octal) + LCD 1.9" ST7789V 170x320 IPS.
//        Variante N = SIN tactil (la C lleva GT911).
//
// Esta placa UNIFICA lo que antes hacian dos: el ESP32-C6 (WiFi/UNAPI) y la
// Pico Zero RP2040 (teclado y joystick USB). El FPGA NO CAMBIA: el bitstream
// existente ya trae cableadas las seis senales en la zona libre 26-32.
//
// ---------------------------------------------------------------------------
// PORQUE ESTE FICHERO EXISTE
// ---------------------------------------------------------------------------
// Antes cada modulo llevaba sus pines a mano y eso ya casi cuesta un disgusto:
// Tape.ino tenia TAPE_TX_PIN=20 (correcto en el C6), pero en ESTA placa el
// GPIO20 es el D+ del USB host. Un pin equivocado aqui no da un error de
// compilacion: da humo o un bus USB muerto. Un solo sitio, revisado, y todos
// los modulos beben de aqui.
//
// ---------------------------------------------------------------------------
// PINOUT DEL LCD - VERIFICADO. NO USAR EL QUE CIRCULA POR INTERNET.
// ---------------------------------------------------------------------------
// Por la red corre un pinout "paralelo 8-bit" para esta placa que es FALSO: es
// un collage de los pines del tactil GT911 de la variante C (que esta placa no
// tiene y que ni siquiera salen a los headers) con pines libres del header, y
// con SCLK/MOSI renombrados como WR/RD. La pantalla es SPI. Confirmado contra
// cinco fuentes independientes, dos de ellas probadas en hardware real.
// Ningun pin del LCD sale a los headers, asi que es imposible pisarlo por error.
// ============================================================================
#ifndef BOARD_S3_H
#define BOARD_S3_H

// Selector de placa. Incluir este fichero es lo que declara que estamos
// compilando para el companion S3; los modulos que difieren entre placas
// (pantalla, USB host) se guardan con este simbolo para que la rama del C6
// -que sigue viva en el MSXimus- no se rompa.
// UNIFICACION 21/08: el SELECTOR de placa se ha movido a Board.h. Este
// fichero se queda con lo suyo, el MAPA DE PINES de la S3. Lo que hay que
// mirar en los modulos son las CAPACIDADES (BOARD_HAS_USB_HOST,
// BOARD_SCREEN_S3), no el nombre de la placa.
#define BOARD_MSXNANO_S3   1

// ---------------------------------------------------------------------------
// LCD ST7789V - SPI (SPI2_HOST, modo 3)
// ---------------------------------------------------------------------------
#define S3_LCD_CS     10
#define S3_LCD_DC     11
#define S3_LCD_SCLK   12
#define S3_LCD_MOSI   13
#define S3_LCD_RST     1
#define S3_LCD_BL     14      // backlight: PWM por LEDC, ACTIVO ALTO, arranca apagado
// Sin MISO (el panel no lo cablea).
#define S3_LCD_W     320      // en horizontal: 320 de ancho x 170 de alto
#define S3_LCD_H     170
#define S3_LCD_XOFF   35      // offset de columna del panel de 170px
// 320 px / 8 = 40 columnas exactas: las mismas que el SCREEN 0 del MSX.

// ---------------------------------------------------------------------------
// USB HOST (teclado + mando, por hub AUTO-ALIMENTADO)
// ---------------------------------------------------------------------------
// El USB-C de la placa va a un CH340 (verificado: SOIC-16 con logo WCH, mas el
// circuito de auto-reset DTR/RTS, y las pull-down 5k1 de CC1/CC2 = puerto sink).
// Por eso el USB nativo del S3 queda ENTERO y libre para hacer de host, y el
// USB-C sigue sirviendo para alimentar y flashear a la vez.
//
// Estos dos son FIJOS por hardware (la matriz GPIO no los remapea):
#define S3_USB_DP     20      // D+ -> header P1, posicion 1
#define S3_USB_DM     19      // D- -> header P1, posicion 2
// El 5V y GND del USB-A salen de P2 posiciones 1 y 2 (misma esquina: cable corto
// y D+/D- adyacentes = par trenzado natural).
//
// AVISO: no uses 19/20 para nada mas.
//
// Limite real del S3: 8 canales de host por hardware, 1 por endpoint.
//   hub 2 + teclado 2 + mando 2 = 6 de 8. Entra, con 2 de margen.
// Lo que NO entra: teclados con hub incorporado, o mandos con vibracion
// (anaden endpoint de salida). Requiere Arduino-ESP32 >= 3.2.0.

// ---------------------------------------------------------------------------
// ENLACE CON EL FPGA - cable plano, 6 senales + GND
// ---------------------------------------------------------------------------
// Las seis caen en seis posiciones FISICAS CONSECUTIVAS al final del header P2,
// en el mismo orden que en el FPGA: el cable va derecho, sin cruces.
//
//   Tang Nano   dir          ESP32-S3     funcion
//   ---------   ----------   ----------   ------------------------------------
//      26       S3 -> FPGA   GPIO38       cinta CVS1 (UART0 remapeada, 115200)
//      27       FPGA -> S3   GPIO39       UNAPI TX del FPGA (UART1 RX, 859372)
//      28       S3 -> FPGA   GPIO40       UNAPI RX del FPGA (UART1 TX, 859372)
//      29       FPGA -> S3   GPIO41       turbo_status (entrada)
//      30          -            -         se salta
//      31       S3 -> FPGA   GPIO42       teclado/joystick (UART2 TX, 115200)
//      32       FPGA -> S3   GPIO2        RTR de la cinta (entrada)
//      GND         -         P2 pos. 2    masa comun
//
// NO SE CONECTA EL 5V ENTRE LAS DOS PLACAS: cada una tiene su propio USB-C, y
// unirlas podria retroalimentar el VBUS del PC. Solo masa comun.
//
// Niveles: el Tang Nano usa LVCMOS33 y el S3 es de 3,3 V -> conexion directa,
// sin adaptacion de niveles.
#define S3_FPGA_TAPE_TX   38  // -> pin 26 FPGA
#define S3_FPGA_UNAPI_RX  39  // <- pin 27 FPGA
#define S3_FPGA_UNAPI_TX  40  // -> pin 28 FPGA
#define S3_FPGA_TURBO     41  // <- pin 29 FPGA (entrada)
#define S3_FPGA_KBD_TX    42  // -> pin 31 FPGA
#define S3_FPGA_TAPE_RTR   2  // <- pin 32 FPGA (entrada)

// ---------------------------------------------------------------------------
// ENLACE CON EL MSXimus (Tang Console 60K) - SPI, NO UART
// ---------------------------------------------------------------------------
// El MSXimus no usa el enlace de arriba: usa el companion SPI que la FPGA YA
// TIENE instanciado (fpga_companion.v + mcu_spi_new.v + hid.v, top.v:5333),
// el de TangCore. Ese bloque estaba pensado para el BL616 de a bordo, que
// nunca llego a llevar firmware, asi que lleva todo este tiempo ocioso
// esperando un maestro. Ver Companion.h para el protocolo.
//
// SE REUTILIZAN LOS MISMOS SEIS PINES FISICOS del enlace del MSXnano, y a
// proposito: son seis posiciones consecutivas al final del header P2, asi que
// EL MISMO CABLE PLANO vale para las dos maquinas. Lo unico que cambia es que
// hablan protocolos distintos, y eso lo decide el firmware, no el cable.
//
//   ESP32-S3     dir          Tang Console 60K        senal SPI
//   ----------   ----------   ---------------------   ----------------------
//   GPIO38       S3 -> FPGA   J10 p20  (U20, GCLKT)   SCLK
//   GPIO40       S3 -> FPGA   J10 p16  (N17)          MOSI  (spi_dat)
//   GPIO42       S3 -> FPGA   J10 p22  (Y21)          CS#   (spi_csn)
//   GPIO39       FPGA -> S3   J10 p14  (W21)          MISO  (spi_dir)
//   GPIO41       FPGA -> S3   J10 p18  (N13)          IRQ#  (spi_irqn)
//   GPIO2        -            -                       libre (sobra uno)
//   GND          -            J10 p12                 masa comun
//
// Las direcciones COINCIDEN con las que ya tiene declarado el .cst del
// MSXimus: N17 es entrada con pull-up, W21 salida con drive 8, N13 salida.
// Y U20 es GCLKT_8, entrada de reloj global: justo lo que quiere spi_sclk,
// que en mcu_spi_new clockea biestables directamente.
//
// ⚠️ EL C6 TIENE QUE SALIR DEL J10: hoy ocupa p14/p16/p18.
// ⚠️ NADA DE 5V entre las dos placas, solo masa. Cada una con su USB-C.
//
// EL BUS SPI TIENE QUE SER EL SPI3_HOST: el SPI2 lo tiene la pantalla.
// Modo 1 (CPOL=0, CPHA=1) y 13,33 MHz, que es a lo que corre el enlace en el
// diseno del FPGA.
#define S3_SPI_SCLK   38      // -> J10 p20
#define S3_SPI_MOSI   40      // -> J10 p16
#define S3_SPI_CS     42      // -> J10 p22
#define S3_SPI_MISO   39      // <- J10 p14
#define S3_SPI_IRQ    41      // <- J10 p18 (entrada, activo BAJO)
#define S3_SPI_HZ     13333333
#define S3_SPI_MODE   1

// ---------------------------------------------------------------------------
// REPARTO DE LAS UARTs - hacen falta 3 TX y solo hay 3 UARTs con la 0 ocupada
// ---------------------------------------------------------------------------
// El truco es la matriz GPIO del ESP32: cualquier UART puede ir a cualquier pin.
// Reasignamos UART0 a nuestro cable EN TIEMPO DE EJECUCION y se sigue flasheando
// con normalidad, porque el bootloader usa sus pines por defecto (43/44 -> CH340)
// antes de que corra nuestro codigo. Lo unico que se pierde es la consola serie:
// la depuracion se va A LA PROPIA PANTALLA, que para eso esta.
//
//   UART0 -> TX GPIO38  cinta CVS1     115200   (consola sacrificada)
//   UART1 -> TX GPIO40 / RX GPIO39     859372   enlace UNAPI con el MSX
//   UART2 -> TX GPIO42  teclado/joy    115200
#define S3_BAUD_UNAPI   859372
#define S3_BAUD_KBD     115200
#define S3_BAUD_TAPE    115200

// Nota: los GPIO 39-42 son ademas los pines de JTAG del S3. Usarlos como GPIO
// es correcto; solo se pierde la depuracion por JTAG, que no usamos (vamos por
// el CH340). GPIO45 es strapping (VDD_SPI): no dejarlo alto en reset -> por eso
// se queda sin usar, y de paso encaja con el pin 30 del FPGA, que tampoco se usa.

// ---------------------------------------------------------------------------
// BOTONES
// ---------------------------------------------------------------------------
#define S3_BTN_BOOT    0      // BOOT: reutilizable como boton de usuario tras arrancar
                              // (rotar vistas de la pantalla). RST es hardware.

// ---------------------------------------------------------------------------
// SIN CONFIRMAR (medir con el polimetro antes de fiarse)
// ---------------------------------------------------------------------------
//  - Modelo del regulador (dos LDO SOT-23-5, marcado ilegible en las fotos):
//    ¿ME6211 de 500 mA o AMS1117 de ~1 A? Presupuestar conservador.
//  - ¿Hay diodo serie entre VBUS y el pin 5V de P2? Irrelevante mientras NO se
//    conecte el 5V entre placas (ver arriba), pero conviene saberlo.
//  - Un post de foro menciona un microinterruptor en GPIO4; en las fotos de esta
//    placa solo se ven RST y BOOT. Se trata GPIO4 como libre.
//
// Comprobacion de 10 segundos antes de soldar el USB-A: continuidad entre los
// D+/D- del conector USB-C y los GPIO 19/20 -> debe dar ABIERTO. Y que el header
// P1 tenga 3,3 V y el P2 tenga 5 V.

#endif // BOARD_S3_H
