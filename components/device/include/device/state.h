#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"

typedef struct {
  esp_netif_ip_info_t *ip_info;
  char *device_name;
} device_state_t;

typedef device_state_t *device_state_handle_t;

esp_err_t device_state_init(device_state_handle_t *handle);