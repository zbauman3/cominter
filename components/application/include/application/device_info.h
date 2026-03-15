#pragma once

#include "esp_err.h"

#include "network/types.h"

typedef struct app_device_info_t {
  char *name;
  // this is the MAC address of the device
  network_mac_address_t mac_address;
} app_device_info_t;

typedef app_device_info_t *app_device_info_handle_t;

esp_err_t
app_device_info_init(app_device_info_handle_t *device_info_handle_ptr);