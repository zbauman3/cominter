#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define NETWORK_EVENT_GOT_NEW_IP (1 << 0)
#define NETWORK_EVENT_LOST_IP (1 << 1)
#define NETWORK_EVENT_SOCKET_READY (1 << 2)

typedef struct network_events_t {
  EventGroupHandle_t group_handle;
} network_events_t;

typedef network_events_t *network_events_handle_t;

esp_err_t network_events_init(network_events_handle_t *handle_ptr);