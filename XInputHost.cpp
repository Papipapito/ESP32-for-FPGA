// XInputHost.cpp - ver XInputHost.h para el porque y el presupuesto de canales.
//
// El fichero tiene DOS mitades bien separadas:
//
//   PARTE A - troceado de reports y clasificacion de interfaces. C++11 puro,
//             sin USB y sin FreeRTOS: se compila en el PC y se verifica en
//             host_test/test_xinput.cpp. Es un port FIEL de xinputh_open() y de
//             xinputh_xfer_cb() (rama IN) de MSXnano/fpga/rp2040/src/xinput_host.c
//             (Ryzee119, MIT), con comprobaciones de longitud anadidas.
//
//   PARTE B - transporte: un CLIENTE ADICIONAL del usb_host de ESP-IDF que
//             convive con el de EspUsbHost. Solo se compila para el ESP32-S3.
//
// Si algo de la PARTE A se desvia del original de Ryzee119 es un BUG, no una
// mejora: el mapeo final (MsxHid::decodeXInput) ya esta congelado y probado.
#include "XInputHost.h"

#include <string.h>

// ===========================================================================
// PARTE A - piezas puras
// ===========================================================================

// Clon de xinputh_open() (xinput_host.c L263-287). El orden de las pruebas
// importa: el Xbox original se distingue por CLASE (0x58), los demas por
// subclase+protocolo dentro de la clase vendor 0xFF.
//
// bNumEndpoints >= 2 se conserva del original aunque aqui solo usemos el IN:
// es la firma real de un mando (IN + OUT). Una interfaz vendor con un solo
// endpoint no es un mando XInput y reclamarla seria tirar canales a la basura.
XInputType xinputClassifyInterface(uint8_t bInterfaceClass,
                                   uint8_t bInterfaceSubClass,
                                   uint8_t bInterfaceProtocol,
                                   uint8_t bNumEndpoints) {
    if (bNumEndpoints < 2) return XINPUT_TYPE_NONE;

    // Xbox original: clase propia 0x58, subclase 0x42. No mira el protocolo.
    if (bInterfaceClass == 0x58 && bInterfaceSubClass == 0x42)
        return XINPUT_TYPE_XBOXOG;

    if (bInterfaceClass != 0xFF) return XINPUT_TYPE_NONE;

    if (bInterfaceSubClass == 0x5D && bInterfaceProtocol == 0x81)
        return XINPUT_TYPE_360_WIRELESS;   // receptor inalambrico 360
    if (bInterfaceSubClass == 0x5D && bInterfaceProtocol == 0x01)
        return XINPUT_TYPE_360_WIRED;      // 360 por cable y la mayoria de clones
    if (bInterfaceSubClass == 0x47 && bInterfaceProtocol == 0xD0)
        return XINPUT_TYPE_XBOXONE;        // One / Series (GIP)

    return XINPUT_TYPE_NONE;
}

namespace {

// Los tres formatos de report empaquetan los 16 botones en little-endian, pero
// en offsets distintos y con ORDEN DE BITS distinto. Se traducen a la mascara
// canonica XINPUT_GAMEPAD_* (la de Ryzee119) porque es la que espera
// MsxHid::decodeXInput().

// 360 (cable e inalambrico) y Xbox original comparten el orden de bits.
uint16_t map360Buttons(uint16_t raw) {
    uint16_t b = 0;
    if (raw & (1u << 0))  b |= XINPUT_GAMEPAD_DPAD_UP;
    if (raw & (1u << 1))  b |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (raw & (1u << 2))  b |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (raw & (1u << 3))  b |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (raw & (1u << 4))  b |= XINPUT_GAMEPAD_START;
    if (raw & (1u << 5))  b |= XINPUT_GAMEPAD_BACK;
    if (raw & (1u << 6))  b |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (raw & (1u << 7))  b |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if (raw & (1u << 8))  b |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (raw & (1u << 9))  b |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (raw & (1u << 10)) b |= XINPUT_GAMEPAD_GUIDE;
    if (raw & (1u << 12)) b |= XINPUT_GAMEPAD_A;
    if (raw & (1u << 13)) b |= XINPUT_GAMEPAD_B;
    if (raw & (1u << 14)) b |= XINPUT_GAMEPAD_X;
    if (raw & (1u << 15)) b |= XINPUT_GAMEPAD_Y;
    return b;
}

// El Xbox original manda los botones de cara como PRESION (0..255), no como
// bits: el bloque digital solo trae cruceta/start/back/thumbs.
uint16_t mapOgDigital(uint16_t raw) {
    uint16_t b = 0;
    if (raw & (1u << 0)) b |= XINPUT_GAMEPAD_DPAD_UP;
    if (raw & (1u << 1)) b |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (raw & (1u << 2)) b |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (raw & (1u << 3)) b |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (raw & (1u << 4)) b |= XINPUT_GAMEPAD_START;
    if (raw & (1u << 5)) b |= XINPUT_GAMEPAD_BACK;
    if (raw & (1u << 6)) b |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (raw & (1u << 7)) b |= XINPUT_GAMEPAD_RIGHT_THUMB;
    return b;
}

inline uint16_t le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[1] << 8 | (uint16_t)p[0]);
}
inline int16_t sle16(const uint8_t* p) {
    return (int16_t)le16(p);
}

} // namespace

// Clon de xinputh_xfer_cb() rama TUSB_DIR_IN (xinput_host.c L387-533).
// DIFERENCIA DELIBERADA: aqui se comprueba la longitud antes de indexar. En el
// original el buffer era un array fijo de 32 bytes que se leia hasta rdata[19]
// sin mirar xferred_bytes; con ESP-IDF llega lo que diga actual_num_bytes y un
// paquete corto haria leer basura del final del buffer.
XInputParse xinputParseReport(XInputType type, const uint8_t* data, size_t len,
                              uint16_t* wButtons, int16_t* thumbLX, int16_t* thumbLY) {
    if (!data || len < 2) return XINPUT_PARSE_IGNORED;

    switch (type) {
    case XINPUT_TYPE_360_WIRED:
        // Cabecera 0x00 0x14 = paquete de estado. Sticks hasta data[13].
        if (data[1] != 0x14 || len < 14) return XINPUT_PARSE_IGNORED;
        if (wButtons) *wButtons = map360Buttons(le16(&data[2]));
        if (thumbLX)  *thumbLX  = sle16(&data[6]);
        if (thumbLY)  *thumbLY  = sle16(&data[8]);
        return XINPUT_PARSE_PAD;

    case XINPUT_TYPE_360_WIRELESS:
        // El receptor multiplexa: primero avisa de conexion/desconexion del
        // mando y solo despues manda estado. Sin esto, un mando apagado dejaria
        // el ultimo estado clavado en el MSX.
        if (data[0] & 0x08) {
            if (data[1] != 0x00) return XINPUT_PARSE_LINK_UP;
            return XINPUT_PARSE_LINK_DOWN;
        }
        // Paquete de botones: bit0 del byte 1 y marca 0x13 en el byte 5.
        if (!(data[1] & 0x01) || len < 18 || data[5] != 0x13) return XINPUT_PARSE_IGNORED;
        if (wButtons) *wButtons = map360Buttons(le16(&data[6]));
        if (thumbLX)  *thumbLX  = sle16(&data[10]);
        if (thumbLY)  *thumbLY  = sle16(&data[12]);
        return XINPUT_PARSE_PAD;

    case XINPUT_TYPE_XBOXONE:
        // GIP_CMD_INPUT. Solo se llega aqui si alguien reclamo la interfaz; hoy
        // NO se reclama (hace falta un OUT de encendido), pero el troceado se
        // deja escrito y probado para el dia que se decida gastar ese OUT.
        if (data[0] != 0x20 || len < 14) return XINPUT_PARSE_IGNORED;
        {
            const uint16_t raw = le16(&data[4]);
            uint16_t b = 0;
            if (raw & (1u << 8))  b |= XINPUT_GAMEPAD_DPAD_UP;
            if (raw & (1u << 9))  b |= XINPUT_GAMEPAD_DPAD_DOWN;
            if (raw & (1u << 10)) b |= XINPUT_GAMEPAD_DPAD_LEFT;
            if (raw & (1u << 11)) b |= XINPUT_GAMEPAD_DPAD_RIGHT;
            if (raw & (1u << 2))  b |= XINPUT_GAMEPAD_START;
            if (raw & (1u << 3))  b |= XINPUT_GAMEPAD_BACK;
            if (raw & (1u << 14)) b |= XINPUT_GAMEPAD_LEFT_THUMB;
            if (raw & (1u << 15)) b |= XINPUT_GAMEPAD_RIGHT_THUMB;
            if (raw & (1u << 12)) b |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            if (raw & (1u << 13)) b |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if (raw & (1u << 4))  b |= XINPUT_GAMEPAD_A;
            if (raw & (1u << 5))  b |= XINPUT_GAMEPAD_B;
            if (raw & (1u << 6))  b |= XINPUT_GAMEPAD_X;
            if (raw & (1u << 7))  b |= XINPUT_GAMEPAD_Y;
            if (wButtons) *wButtons = b;
            if (thumbLX)  *thumbLX  = sle16(&data[10]);
            if (thumbLY)  *thumbLY  = sle16(&data[12]);
        }
        return XINPUT_PARSE_PAD;

    case XINPUT_TYPE_XBOXOG:
        // Mismo encabezado que el 360 por cable pero cuerpo distinto: A/B/X/Y
        // son analogicos (umbral 0x20, como en el original) y los sticks estan
        // 6 bytes mas alla.
        if (data[1] != 0x14 || len < 20) return XINPUT_PARSE_IGNORED;
        {
            uint16_t b = mapOgDigital(le16(&data[2]));
            if (data[4] > 0x20) b |= XINPUT_GAMEPAD_A;
            if (data[5] > 0x20) b |= XINPUT_GAMEPAD_B;
            if (data[6] > 0x20) b |= XINPUT_GAMEPAD_X;
            if (data[7] > 0x20) b |= XINPUT_GAMEPAD_Y;
            if (data[8] > 0x20) b |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if (data[9] > 0x20) b |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            if (wButtons) *wButtons = b;
            if (thumbLX)  *thumbLX  = sle16(&data[12]);
            if (thumbLY)  *thumbLY  = sle16(&data[14]);
        }
        return XINPUT_PARSE_PAD;

    case XINPUT_TYPE_NONE:
    default:
        return XINPUT_PARSE_IGNORED;
    }
}

// ===========================================================================
// PARTE B - transporte (solo ESP32-S3)
// ===========================================================================
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <usb/usb_host.h>
#include <usb/usb_helpers.h>
#include <esp_log.h>
#include <esp_err.h>

// Cuantos mandos XInput a la vez. El presupuesto de canales del S3 (ver el .h)
// solo da para UNO junto a hub + teclado; se deja en 2 porque el segundo se
// resuelve solo: usb_host_interface_claim() devolvera ESP_ERR_NO_MEM al quedarse
// sin canales, se registra en el log y no pasa nada mas.
#ifndef XINPUTHOST_MAX_PADS
#define XINPUTHOST_MAX_PADS 2
#endif

// Buffer de un report. FS limita el interrupt a 64 bytes/paquete; el 360 usa 32
// y el One 64. Se reserva el maximo y ya.
#define XINPUTHOST_MAX_MPS 64

// Errores seguidos antes de rendirse con un mando (evita quemar CPU con un
// endpoint que no arranca). A 250 Hz nominales son ~4 s de insistencia.
#define XINPUTHOST_MAX_ERRORS 1000

// Cada cuanto se repasa el bus buscando mandos que ya estaban enchufados. Ver
// abajo por que hace falta ademas del evento NEW_DEV.
#define XINPUTHOST_RESCAN_MS 1000

static const char* TAG = "XInputHost";

namespace {

struct PadSlot {
    bool                inUse;
    bool                closing;      // el mando se fue: hay que soltar y cerrar
    bool                pendingClear; // hubo STALL: limpiar EP desde la tarea
    bool                pendingSubmit;// re-armar desde la tarea (tras error/STALL)
    bool                inflight;     // hay una transferencia viva
    bool                linkUp;       // receptores 360: hay mando encendido?
    uint8_t             address;
    XInputType          type;
    usb_device_handle_t dev;
    uint8_t             itfNum;
    uint8_t             epIn;
    uint16_t            mps;
    usb_transfer_t*     xfer;
    uint32_t            errors;
    uint32_t            closeDeadline;
};

usb_host_client_handle_t s_client = NULL;
TaskHandle_t             s_task   = NULL;
bool                     s_running = false;

XInputReportCb s_onReport = NULL;
XInputMountCb  s_onMount  = NULL;
XInputUmountCb s_onUmount = NULL;

PadSlot s_slots[XINPUTHOST_MAX_PADS];

// Cola diminuta de direcciones que anuncia el callback de eventos. El callback
// corre DENTRO de usb_host_client_handle_events(); abrir dispositivos y andar
// reclamando interfaces desde ahi es pedir problemas de reentrada, asi que solo
// se apunta la direccion y el trabajo real lo hace el bucle de la tarea (mismo
// patron que el ejemplo usb_host_lib de ESP-IDF).
//
// NO hace falta ni volatile ni cerrojo: el callback y el bucle corren en la
// MISMA tarea (el callback sale de usb_host_client_handle_events(), que llama
// taskLoop). Nadie de fuera toca estas variables.
uint8_t s_pending[8];
uint8_t s_pendingCount = 0;

// Mapa de bits de direcciones YA DESCARTADAS (miradas y no eran un mando).
// Sin esto, el repaso periodico abriria y cerraria el hub y el teclado una vez
// por segundo para siempre. Las direcciones USB van de 1 a 127 -> 16 bytes.
uint8_t s_rejected[16];

inline bool isRejected(uint8_t a)   { return a < 128 && (s_rejected[a >> 3] & (uint8_t)(1u << (a & 7))); }
inline void markRejected(uint8_t a) { if (a < 128) s_rejected[a >> 3] |= (uint8_t)(1u << (a & 7)); }
inline void clearRejected(uint8_t a){ if (a < 128) s_rejected[a >> 3] &= (uint8_t)~(1u << (a & 7)); }

uint32_t nowMs() { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

PadSlot* slotByDevice(usb_device_handle_t dev) {
    for (int i = 0; i < XINPUTHOST_MAX_PADS; i++)
        if (s_slots[i].inUse && s_slots[i].dev == dev) return &s_slots[i];
    return NULL;
}

bool addressTracked(uint8_t address) {
    for (int i = 0; i < XINPUTHOST_MAX_PADS; i++)
        if (s_slots[i].inUse && s_slots[i].address == address) return true;
    return false;
}

PadSlot* freeSlot() {
    for (int i = 0; i < XINPUTHOST_MAX_PADS; i++)
        if (!s_slots[i].inUse) return &s_slots[i];
    return NULL;
}

void pushPending(uint8_t address) {
    if (s_pendingCount >= sizeof(s_pending)) return;   // desbordar es inofensivo:
    s_pending[s_pendingCount++] = address;             // el rescan periodico lo pilla
}

// -----------------------------------------------------------------------
// Callback de la transferencia. Corre en la tarea de este modulo, dentro de
// usb_host_client_handle_events(). Se re-arma AQUI MISMO en el caso bueno
// (igual que hace el driver HID oficial de Espressif, hid_host.c in_xfer_done):
// diferirlo al bucle costaria una vuelta entera de espera de eventos y
// limitaria el sondeo del mando al periodo del bucle.
// -----------------------------------------------------------------------
void xferDone(usb_transfer_t* xfer) {
    PadSlot* s = (PadSlot*)xfer->context;
    if (!s) return;
    s->inflight = false;

    switch (xfer->status) {
    case USB_TRANSFER_STATUS_COMPLETED: {
        s->errors = 0;
        uint16_t buttons = 0;
        int16_t  lx = 0, ly = 0;
        const XInputParse r = xinputParseReport(s->type, xfer->data_buffer,
                                                (size_t)xfer->actual_num_bytes,
                                                &buttons, &lx, &ly);
        if (r == XINPUT_PARSE_PAD) {
            s->linkUp = true;
            if (s_onReport) s_onReport(s->address, buttons, lx, ly);
        } else if (r == XINPUT_PARSE_LINK_UP) {
            s->linkUp = true;
        } else if (r == XINPUT_PARSE_LINK_DOWN) {
            // Mando inalambrico apagado: hay que soltar todo o el MSX se queda
            // con la ultima direccion pulsada para siempre.
            s->linkUp = false;
            if (s_onReport) s_onReport(s->address, 0, 0, 0);
        }
        if (!s->closing) {
            s->inflight = true;
            if (usb_host_transfer_submit(xfer) != ESP_OK) {
                s->inflight = false;
                s->pendingSubmit = true;   // reintento desde la tarea
            }
        }
        return;
    }

    case USB_TRANSFER_STATUS_NO_DEVICE:
    case USB_TRANSFER_STATUS_CANCELED:
        // El desmontaje lo dispara DEV_GONE; aqui solo se deja de insistir.
        s->closing = true;
        return;

    case USB_TRANSFER_STATUS_STALL:
        // CLEAR_FEATURE(HALT) es una transferencia de control: NO se lanza desde
        // dentro del manejador de eventos, se aparca para el bucle.
        s->pendingClear  = true;
        s->pendingSubmit = true;
        return;

    default:
        if (++s->errors > XINPUTHOST_MAX_ERRORS) {
            ESP_LOGW(TAG, "addr=%u demasiados errores (status=%d), se abandona",
                     s->address, (int)xfer->status);
            s->closing = true;
        } else {
            s->pendingSubmit = true;
        }
        return;
    }
}

bool submitIn(PadSlot* s) {
    if (!s->xfer || s->inflight || s->closing) return false;
    s->xfer->device_handle    = s->dev;
    s->xfer->bEndpointAddress = s->epIn;
    s->xfer->num_bytes        = s->mps;   // IN: multiplo del MPS, obligatorio
    s->xfer->callback         = xferDone;
    s->xfer->context          = s;
    s->xfer->timeout_ms       = 0;        // interrupt-IN: se queda esperando datos
    s->inflight = true;
    const esp_err_t err = usb_host_transfer_submit(s->xfer);
    if (err != ESP_OK) {
        s->inflight = false;
        ESP_LOGW(TAG, "usb_host_transfer_submit(addr=%u) fallo: %s",
                 s->address, esp_err_to_name(err));
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// Sonda un dispositivo recien visto. Devuelve true si se ha quedado con el.
//
// IMPORTANTE: si NO es un mando XInput hay que cerrarlo inmediatamente. Cada
// usb_host_device_open() es una referencia; dejarla abierta impediria que el
// stack libere el dispositivo al desenchufarlo.
// -----------------------------------------------------------------------
bool probeDevice(uint8_t address) {
    if (addressTracked(address) || isRejected(address)) return false;

    usb_device_handle_t dev = NULL;
    esp_err_t err = usb_host_device_open(s_client, address, &dev);
    if (err != ESP_OK) {
        // ESP_ERR_NOT_FOUND es normal: el dispositivo pudo irse entre el aviso
        // y esta llamada. No se marca como descartado: si vuelve, se reintenta.
        ESP_LOGD(TAG, "usb_host_device_open(%u) fallo: %s", address, esp_err_to_name(err));
        return false;
    }

    const usb_config_desc_t* cfg = NULL;
    err = usb_host_get_active_config_descriptor(dev, &cfg);
    if (err != ESP_OK || !cfg) {
        usb_host_device_close(s_client, dev);
        return false;
    }

    // Recorrido de TODAS las interfaces del descriptor de configuracion. No se
    // usa usb_parse_interface_descriptor() porque exige saber de antemano el
    // bInterfaceNumber, y aqui justamente estamos buscando cual es.
    int offset = 0;
    const usb_standard_desc_t* d =
        usb_parse_next_descriptor_of_type((const usb_standard_desc_t*)cfg,
                                          cfg->wTotalLength,
                                          USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                          &offset);
    while (d) {
        const usb_intf_desc_t* itf = (const usb_intf_desc_t*)d;
        const int itfOffset = offset;

        // Las interfaces XInput solo tienen alt 0; saltarse las demas evita
        // reclamar dos veces la misma interfaz.
        if (itf->bAlternateSetting == 0) {
            const XInputType type = xinputClassifyInterface(itf->bInterfaceClass,
                                                            itf->bInterfaceSubClass,
                                                            itf->bInterfaceProtocol,
                                                            itf->bNumEndpoints);
            if (type != XINPUT_TYPE_NONE) {
                ESP_LOGI(TAG, "XInput addr=%u iface=%u class=0x%02x/0x%02x/0x%02x tipo=%d",
                         address, itf->bInterfaceNumber, itf->bInterfaceClass,
                         itf->bInterfaceSubClass, itf->bInterfaceProtocol, (int)type);

                if (type == XINPUT_TYPE_XBOXONE) {
                    // DECISION EXPLICITA, no un olvido: un mando One/Series no
                    // emite NADA hasta recibir por interrupt-OUT el paquete GIP
                    // de encendido (0x05 0x20 0x00 0x01 0x00, ver xboxone_init()
                    // en xinput_host.c). Este driver es solo-IN por contrato
                    // (herencia del RP2040, donde el OUT rompia el mando detras
                    // de un hub), asi que ni se reclama: reclamarlo gastaria 2
                    // canales de los 8 del S3 para no leer nada nunca.
                    ESP_LOGW(TAG, "Mando Xbox One/Series (addr=%u): NO soportado, "
                                  "necesita un paquete de encendido por OUT. Usa un "
                                  "mando estilo 360 o pon el pad en modo DirectInput.",
                             address);
                    d = usb_parse_next_descriptor_of_type(d, cfg->wTotalLength,
                                                          USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                                          &offset);
                    continue;
                }

                // Buscar el primer endpoint de INTERRUPCION-IN de la interfaz.
                uint8_t  epIn = 0;
                uint16_t mps  = 0;
                for (int i = 0; i < itf->bNumEndpoints; i++) {
                    int epOffset = itfOffset;   // in/out: se destroza en cada vuelta
                    const usb_ep_desc_t* ep =
                        usb_parse_endpoint_descriptor_by_index(itf, i, cfg->wTotalLength,
                                                               &epOffset);
                    if (!ep) continue;
                    if (USB_EP_DESC_GET_XFERTYPE(ep) != USB_TRANSFER_TYPE_INTR) continue;
                    if (!USB_EP_DESC_GET_EP_DIR(ep)) continue;   // 0 = OUT, no interesa
                    epIn = ep->bEndpointAddress;
                    mps  = (uint16_t)USB_EP_DESC_GET_MPS(ep);
                    break;
                }
                if (!epIn || mps == 0 || mps > XINPUTHOST_MAX_MPS) {
                    ESP_LOGW(TAG, "iface=%u sin interrupt-IN usable (ep=0x%02x mps=%u)",
                             itf->bInterfaceNumber, epIn, mps);
                    d = usb_parse_next_descriptor_of_type(d, cfg->wTotalLength,
                                                          USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                                          &offset);
                    continue;
                }

                PadSlot* s = freeSlot();
                if (!s) {
                    ESP_LOGW(TAG, "sin hueco para mas mandos XInput");
                    break;
                }

                // OJO: esto abre TODOS los endpoints de la interfaz (IN y OUT),
                // no solo el que vamos a usar. Es como funciona ESP-IDF
                // (usb_host.c interface_claim -> ep_wrapper_alloc por cada
                // bNumEndpoints) y por eso el mando cuesta 3 canales, no 2.
                err = usb_host_interface_claim(s_client, dev,
                                               itf->bInterfaceNumber,
                                               itf->bAlternateSetting);
                if (err != ESP_OK) {
                    // ESP_ERR_NO_MEM aqui = te has quedado sin canales HCD.
                    ESP_LOGE(TAG, "usb_host_interface_claim(%u) fallo: %s "
                                  "(sin canales? hub+teclado+mando = 7 de 8)",
                             itf->bInterfaceNumber, esp_err_to_name(err));
                    break;
                }

                s->inUse         = true;
                s->closing       = false;
                s->pendingClear  = false;
                s->pendingSubmit = false;
                s->inflight      = false;
                // Un mando por cable ya esta "conectado"; en el receptor
                // inalambrico hay que esperar al paquete de enlace.
                s->linkUp        = (type != XINPUT_TYPE_360_WIRELESS);
                s->address       = address;
                s->type          = type;
                s->dev           = dev;
                s->itfNum        = itf->bInterfaceNumber;
                s->epIn          = epIn;
                s->mps           = mps;
                s->errors        = 0;
                s->closeDeadline = 0;

                if (s_onMount) s_onMount(address, type);
                if (!submitIn(s)) s->pendingSubmit = true;

                ESP_LOGI(TAG, "mando listo addr=%u iface=%u ep=0x%02x mps=%u",
                         address, s->itfNum, s->epIn, s->mps);
                return true;   // un solo mando por dispositivo: nos sobra
            }
        }

        d = usb_parse_next_descriptor_of_type(d, cfg->wTotalLength,
                                              USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                              &offset);
    }

    usb_host_device_close(s_client, dev);   // no era un mando: soltar la referencia
    markRejected(address);                  // y no volver a mirarlo cada segundo
    return false;
}

// Suelta interfaz + dispositivo. SOLO cuando no queda ninguna transferencia
// viva: usb_host_interface_release() exige que los endpoints esten quietos.
// El usb_transfer_t NO se libera nunca (se reserva una vez por hueco y se
// reutiliza): asi es imposible liberar un buffer que el hardware aun toca.
void finishClose(PadSlot* s) {
    if (s->inflight) {
        if (s->closeDeadline == 0) s->closeDeadline = nowMs() + 2000;
        if ((int32_t)(nowMs() - s->closeDeadline) < 0) return;   // aun hay margen
        ESP_LOGW(TAG, "addr=%u: transferencia sin completar al cerrar", s->address);
        s->inflight = false;   // el dispositivo ya no esta; el URB no volvera
    }
    if (s->dev) {
        usb_host_interface_release(s_client, s->dev, s->itfNum);
        usb_host_device_close(s_client, s->dev);
    }
    const uint8_t address = s->address;
    // Se limpia TODO menos xfer: ese buffer se reserva una vez en
    // ensureTransfers() y se reutiliza mientras viva el firmware.
    usb_transfer_t* keep = s->xfer;
    memset(s, 0, sizeof(*s));
    s->xfer = keep;
    if (s_onUmount) s_onUmount(address);
    ESP_LOGI(TAG, "mando fuera addr=%u", address);
}

void clientEventCb(const usb_host_client_event_msg_t* msg, void* arg) {
    (void)arg;
    switch (msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        // Las direcciones se reciclan: si aqui hay uno nuevo, lo que se
        // descarto antes con esta direccion ya no cuenta.
        clearRejected(msg->new_dev.address);
        pushPending(msg->new_dev.address);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE: {
        PadSlot* s = slotByDevice(msg->dev_gone.dev_hdl);
        if (s) s->closing = true;
        break;
    }
    default:
        break;
    }
}

// Buffers de transferencia: uno por hueco, reservados una sola vez y jamas
// liberados (ver finishClose).
usb_transfer_t* s_xfers[XINPUTHOST_MAX_PADS];

bool ensureTransfers() {
    for (int i = 0; i < XINPUTHOST_MAX_PADS; i++) {
        if (!s_xfers[i]) {
            const esp_err_t err = usb_host_transfer_alloc(XINPUTHOST_MAX_MPS, 0, &s_xfers[i]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "usb_host_transfer_alloc fallo: %s", esp_err_to_name(err));
                return false;
            }
        }
        s_slots[i].xfer = s_xfers[i];
    }
    return true;
}

void serviceSlots() {
    for (int i = 0; i < XINPUTHOST_MAX_PADS; i++) {
        PadSlot* s = &s_slots[i];
        if (!s->inUse) continue;

        if (s->closing) {
            finishClose(s);
            continue;
        }
        if (s->pendingClear) {
            s->pendingClear = false;
            const esp_err_t err = usb_host_endpoint_clear(s->dev, s->epIn);
            if (err != ESP_OK)
                ESP_LOGW(TAG, "usb_host_endpoint_clear(0x%02x) fallo: %s",
                         s->epIn, esp_err_to_name(err));
        }
        if (s->pendingSubmit && !s->inflight) {
            s->pendingSubmit = false;
            if (!submitIn(s)) s->pendingSubmit = true;   // se reintenta la vuelta que viene
        }
    }
}

// Repaso periodico del bus. HACE FALTA aunque tengamos NEW_DEV: ESP-IDF manda
// ese evento solo a los clientes registrados EN EL MOMENTO de la enumeracion
// (usb_host.c, send_event_msg_to_clients(); no hay repeticion para los que
// llegan tarde). Como este cliente se registra despues del de EspUsbHost, un
// mando ya enchufado al encender la placa podria enumerarse antes y no
// generarnos ningun evento. usb_host_device_addr_list_fill() lo resuelve.
void rescan() {
    uint8_t addrs[16];
    int count = 0;
    if (usb_host_device_addr_list_fill((int)sizeof(addrs), addrs, &count) != ESP_OK) return;

    // Los descartes caducan cuando el dispositivo desaparece del bus: asi una
    // direccion reciclada por un mando se vuelve a mirar.
    uint8_t present[sizeof(s_rejected)];
    memset(present, 0, sizeof(present));
    for (int i = 0; i < count; i++) {
        const uint8_t a = addrs[i];
        if (a < 128) present[a >> 3] |= (uint8_t)(1u << (a & 7));
    }
    for (size_t i = 0; i < sizeof(s_rejected); i++) s_rejected[i] &= present[i];

    for (int i = 0; i < count; i++) probeDevice(addrs[i]);
}

void taskLoop(void* arg) {
    (void)arg;
    uint32_t nextRescan = 0;

    while (s_running) {
        // Dispara los callbacks de eventos y de transferencia. El timeout corto
        // mantiene vivo el mantenimiento de abajo aunque no pase nada.
        usb_host_client_handle_events(s_client, pdMS_TO_TICKS(10));

        while (s_pendingCount > 0) {
            const uint8_t addr = s_pending[--s_pendingCount];
            probeDevice(addr);
        }

        serviceSlots();

        const uint32_t t = nowMs();
        if ((int32_t)(t - nextRescan) >= 0) {
            nextRescan = t + XINPUTHOST_RESCAN_MS;
            rescan();
        }
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

} // namespace

bool xinputHostBegin(XInputReportCb onReport, XInputMountCb onMount,
                     XInputUmountCb onUmount, uint32_t readyTimeoutMs) {
    if (s_client) return true;          // ya arrancado
    if (!onReport) return false;

    s_onReport = onReport;
    s_onMount  = onMount;
    s_onUmount = onUmount;
    memset(s_slots, 0, sizeof(s_slots));
    memset(s_rejected, 0, sizeof(s_rejected));

    // usb_host_install() es de EspUsbHost, y lo hace DENTRO de su tarea: al
    // volver de EspUsbHost::begin() puede no estar hecho todavia. Mientras la
    // libreria no este instalada, usb_host_client_register() devuelve
    // ESP_ERR_INVALID_STATE; se reintenta hasta agotar readyTimeoutMs.
    usb_host_client_config_t cfg = {};
    cfg.is_synchronous = false;                 // cliente asincrono (el unico modo)
    cfg.max_num_event_msg = 8;
    cfg.async.client_event_callback = clientEventCb;
    cfg.async.callback_arg = NULL;

    esp_err_t err = ESP_ERR_INVALID_STATE;
    const uint32_t deadline = nowMs() + readyTimeoutMs;
    while (true) {
        err = usb_host_client_register(&cfg, &s_client);
        if (err == ESP_OK) break;
        if ((int32_t)(nowMs() - deadline) >= 0) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_client_register() fallo: %s "
                      "(se ha llamado despues de EspUsbHost::begin()?)",
                 esp_err_to_name(err));
        s_client = NULL;
        return false;
    }

    if (!ensureTransfers()) {
        usb_host_client_deregister(s_client);
        s_client = NULL;
        return false;
    }

    s_running = true;
    // Prioridad 5 y 4 KB: el mismo orden de magnitud que la tarea cliente de
    // EspUsbHost. Sin afinidad a nucleo: aqui no hay nada critico en tiempo,
    // el hardware ya sondea el endpoint por su cuenta al ritmo del bInterval.
    if (xTaskCreate(taskLoop, "XInputHost", 4096, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "no se pudo crear la tarea");
        s_running = false;
        usb_host_client_deregister(s_client);
        s_client = NULL;
        return false;
    }

    ESP_LOGI(TAG, "cliente XInput registrado (max %d mandos)", XINPUTHOST_MAX_PADS);
    return true;
}

uint8_t xinputHostPadCount() {
    uint8_t n = 0;
    for (int i = 0; i < XINPUTHOST_MAX_PADS; i++)
        if (s_slots[i].inUse && s_slots[i].linkUp) n++;
    return n;
}

#else

// Placas sin USB host (el ESP32-C6 de la version vieja) y compilacion en el PC:
// stubs para que el .ino compile sin #ifdefs y para que el banco de pruebas
// enlace. La PARTE A si se compila siempre: es la que se verifica.
bool xinputHostBegin(XInputReportCb, XInputMountCb, XInputUmountCb, uint32_t) { return false; }
uint8_t xinputHostPadCount() { return 0; }

#endif // CONFIG_IDF_TARGET_ESP32S3
