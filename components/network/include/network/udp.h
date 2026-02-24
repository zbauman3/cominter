#pragma once

#include "application/state.h"
#include "esp_err.h"

#define NETWORK_UDP_HEARTBEAT_INTERVAL_MS 10000

esp_err_t network_udp_init(app_state_handle_t state_handle);
void network_udp_socket_close(app_state_handle_t state_handle);