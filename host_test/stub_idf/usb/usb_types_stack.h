// Stub de usb/usb_types_stack.h (ESP-IDF v5.5). Transcrito, no inventado.
// OJO con los const de usb_transfer_s: son reales. data_buffer y
// data_buffer_size los fija usb_host_transfer_alloc() y NO se pueden reasignar;
// si el codigo lo intentara, este stub lo caza igual que el compilador de la placa.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
    USB_TRANSFER_TYPE_CTRL = 0,
    USB_TRANSFER_TYPE_ISOCHRONOUS,
    USB_TRANSFER_TYPE_BULK,
    USB_TRANSFER_TYPE_INTR,
} usb_transfer_type_t;

typedef enum {
    USB_SPEED_LOW = 0,
    USB_SPEED_FULL,
    USB_SPEED_HIGH,
} usb_speed_t;

typedef enum {
    USB_TRANSFER_STATUS_COMPLETED,
    USB_TRANSFER_STATUS_ERROR,
    USB_TRANSFER_STATUS_TIMED_OUT,
    USB_TRANSFER_STATUS_CANCELED,
    USB_TRANSFER_STATUS_STALL,
    USB_TRANSFER_STATUS_OVERFLOW,
    USB_TRANSFER_STATUS_SKIPPED,
    USB_TRANSFER_STATUS_NO_DEVICE,
} usb_transfer_status_t;

typedef struct {
    int num_bytes;
    int actual_num_bytes;
    usb_transfer_status_t status;
} usb_isoc_packet_desc_t;

typedef struct usb_transfer_s usb_transfer_t;
typedef void (*usb_transfer_cb_t)(usb_transfer_t *transfer);
typedef void *usb_device_handle_t;

#define USB_TRANSFER_FLAG_ZERO_PACK 0x01

struct usb_transfer_s {
    uint8_t *const data_buffer;
    const size_t data_buffer_size;
    int num_bytes;
    int actual_num_bytes;
    uint32_t flags;
    usb_device_handle_t device_handle;
    uint8_t bEndpointAddress;
    usb_transfer_status_t status;
    uint32_t timeout_ms;
    usb_transfer_cb_t callback;
    void *context;
    const int num_isoc_packets;
    usb_isoc_packet_desc_t isoc_packet_desc[];
};
