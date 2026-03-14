#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct network_queues_t {
  // This contains a pointer to a message.
  // Readers must free the message after use.
  // Writers must not interact with the message.
  QueueHandle_t message_outgoing;
  // This contains a pointer to a message.
  // Readers must free the message after use.
  // Writers must not interact with the message.
  QueueHandle_t message_incoming;
} network_queues_t;

typedef network_queues_t *network_queues_handle_t;

esp_err_t network_queues_init(network_queues_handle_t *handle_ptr);