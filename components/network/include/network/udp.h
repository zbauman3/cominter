#pragma once

#include "application/state.h"
#include "esp_err.h"

esp_err_t udp_multicast_init(state_handle_t state_handle);
void udp_socket_close(state_handle_t state_handle);