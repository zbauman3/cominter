#pragma once

#include "esp_err.h"

#include "device/state.h"

#define DEVICE_INFO_NAMESPACE "device_info"
#define DEVICE_INFO_NAME_KEY "name"

esp_err_t persistence_init();
esp_err_t persistence_fetch_name(device_state_handle_t device_state_handle);