// Stub de usb/usb_host.h (ESP-IDF v5.5). Solo lo que usa XInputHost.cpp, con
// las firmas EXACTAS del original. Transcrito, no inventado.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "usb/usb_types_stack.h"
#include "usb/usb_types_ch9.h"

typedef void *usb_host_client_handle_t;

typedef enum {
    USB_HOST_CLIENT_EVENT_NEW_DEV,
    USB_HOST_CLIENT_EVENT_DEV_GONE,
} usb_host_client_event_t;

typedef struct {
    usb_host_client_event_t event;
    union {
        struct {
            uint8_t address;
        } new_dev;
        struct {
            usb_device_handle_t dev_hdl;
        } dev_gone;
    };
} usb_host_client_event_msg_t;

typedef void (*usb_host_client_event_cb_t)(const usb_host_client_event_msg_t *event_msg, void *arg);

typedef struct {
    bool is_synchronous;
    int max_num_event_msg;
    union {
        struct {
            usb_host_client_event_cb_t client_event_callback;
            void *callback_arg;
        } async;
    };
} usb_host_client_config_t;

esp_err_t usb_host_client_register(const usb_host_client_config_t *client_config,
                                   usb_host_client_handle_t *client_hdl_ret);
esp_err_t usb_host_client_deregister(usb_host_client_handle_t client_hdl);
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client_hdl,
                                        TickType_t timeout_ticks);

esp_err_t usb_host_device_open(usb_host_client_handle_t client_hdl,
                               uint8_t dev_addr,
                               usb_device_handle_t *dev_hdl_ret);
esp_err_t usb_host_device_close(usb_host_client_handle_t client_hdl,
                                usb_device_handle_t dev_hdl);
esp_err_t usb_host_device_addr_list_fill(int list_len,
                                         uint8_t *dev_addr_list,
                                         int *num_dev_ret);
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t dev_hdl,
                                         const usb_device_desc_t **device_desc);
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t dev_hdl,
                                                const usb_config_desc_t **config_desc);

esp_err_t usb_host_interface_claim(usb_host_client_handle_t client_hdl,
                                   usb_device_handle_t dev_hdl,
                                   uint8_t bInterfaceNumber,
                                   uint8_t bAlternateSetting);
esp_err_t usb_host_interface_release(usb_host_client_handle_t client_hdl,
                                     usb_device_handle_t dev_hdl,
                                     uint8_t bInterfaceNumber);

esp_err_t usb_host_endpoint_halt(usb_device_handle_t dev_hdl, uint8_t bEndpointAddress);
esp_err_t usb_host_endpoint_flush(usb_device_handle_t dev_hdl, uint8_t bEndpointAddress);
esp_err_t usb_host_endpoint_clear(usb_device_handle_t dev_hdl, uint8_t bEndpointAddress);

esp_err_t usb_host_transfer_alloc(size_t data_buffer_size, int num_isoc_packets,
                                  usb_transfer_t **transfer);
esp_err_t usb_host_transfer_free(usb_transfer_t *transfer);
esp_err_t usb_host_transfer_submit(usb_transfer_t *transfer);
esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t client_hdl,
                                           usb_transfer_t *transfer);
