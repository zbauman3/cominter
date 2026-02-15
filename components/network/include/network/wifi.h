#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"

esp_err_t wifi_init(esp_netif_ip_info_t **ip_info);