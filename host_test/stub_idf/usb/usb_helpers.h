// Stub de usb/usb_helpers.h (ESP-IDF v5.5). Transcrito, no inventado.
// Los semanticos de `offset` son los del original: entra el offset del
// descriptor actual y sale el del devuelto.
#pragma once
#include <stdint.h>
#include "usb/usb_types_ch9.h"

const usb_standard_desc_t *usb_parse_next_descriptor(const usb_standard_desc_t *cur_desc,
                                                     uint16_t wTotalLength, int *offset);

const usb_standard_desc_t *usb_parse_next_descriptor_of_type(const usb_standard_desc_t *cur_desc,
                                                             uint16_t wTotalLength,
                                                             uint8_t bDescriptorType,
                                                             int *offset);

int usb_parse_interface_number_of_alternate(const usb_config_desc_t *config_desc,
                                            uint8_t bInterfaceNumber);

const usb_intf_desc_t *usb_parse_interface_descriptor(const usb_config_desc_t *config_desc,
                                                      uint8_t bInterfaceNumber,
                                                      uint8_t bAlternateSetting,
                                                      int *offset);

const usb_ep_desc_t *usb_parse_endpoint_descriptor_by_index(const usb_intf_desc_t *intf_desc,
                                                            int index,
                                                            uint16_t wTotalLength,
                                                            int *offset);

const usb_ep_desc_t *usb_parse_endpoint_descriptor_by_address(const usb_config_desc_t *config_desc,
                                                              uint8_t bInterfaceNumber,
                                                              uint8_t bAlternateSetting,
                                                              uint8_t bEndpointAddress,
                                                              int *offset);

static inline int usb_round_up_to_mps(int num_bytes, int mps)
{
    if (num_bytes <= 0 || mps <= 0) {
        return 0;
    }
    return ((num_bytes + mps - 1) / mps) * mps;
}
