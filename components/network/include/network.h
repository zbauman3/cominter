#pragma once

#include "esp_err.h"

#include "network/peers.h"
#include "network/wifi.h"

typedef struct network_t {
  network_peers_list_handle_t peers_list;
  network_wifi_handle_t wifi;
} network_t;

typedef network_t *network_handle_t;

esp_err_t network_init(network_handle_t *network_handle_ptr);
void network_free(network_handle_t network_handle);