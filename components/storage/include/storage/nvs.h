#pragma once

#include "esp_err.h"

#define NVS_DEVICE_INFO_NAMESPACE "device_info"
#define NVS_DEVICE_INFO_NAME_KEY "name"

esp_err_t storage_nvs_init();
esp_err_t storage_nvs_get_name(char **name_ptr);