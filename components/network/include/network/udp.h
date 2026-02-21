#pragma once

#include "application/state.h"
#include "esp_err.h"

esp_err_t udp_multicast_init(device_state_handle_t device_state_handle);
void udp_socket_close(device_state_handle_t device_state_handle);