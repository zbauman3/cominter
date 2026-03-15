#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "application/messages.h"

// unknown messages are dropped.
// audio messages are processed differently.
typedef struct app_queues_t {
  // This contains a pointer to a message.
  // Readers must free the message after use.
  // Writers must not interact with the message after writing.
  QueueHandle_t outgoing;
  // This contains a pointer to a message.
  // Readers must free the message after use.
  // Writers must not interact with the message after writing.
  QueueHandle_t incoming_heartbeat;
  // This contains a pointer to a message.
  // Readers must free the message after use.
  // Writers must not interact with the message after writing.
  QueueHandle_t incoming_text;
} app_queues_t;

typedef app_queues_t *app_queues_handle_t;

esp_err_t app_queues_init(app_queues_handle_t *handle_ptr);

esp_err_t app_queues_receive_outgoing_message(app_queues_handle_t queues_handle,
                                              app_message_handle_t *message_ptr,
                                              TickType_t ticks_to_wait);

esp_err_t app_queues_add_outgoing_message(app_queues_handle_t queues_handle,
                                          app_message_handle_t *message_ptr,
                                          TickType_t ticks_to_wait,
                                          bool send_to_front);

esp_err_t app_queues_receive_incoming_message(app_queues_handle_t queues_handle,
                                              app_message_handle_t *message_ptr,
                                              app_message_type_t type,
                                              TickType_t ticks_to_wait);

esp_err_t app_queues_add_incoming_message(app_queues_handle_t queues_handle,
                                          app_message_handle_t *message_ptr,
                                          TickType_t ticks_to_wait);