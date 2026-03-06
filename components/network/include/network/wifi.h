#pragma once

#include "esp_err.h"

#include "network/events.h"
#include "network/udp.h"

typedef struct network_wifi_t {
  network_udp_handle_t udp;
  network_events_handle_t events;
} network_wifi_t;

typedef network_wifi_t *network_wifi_handle_t;

esp_err_t network_wifi_init(network_wifi_handle_t *wifi_handle,
                            network_events_handle_t events,
                            network_udp_handle_t udp);
void network_wifi_free(network_wifi_handle_t wifi_handle);