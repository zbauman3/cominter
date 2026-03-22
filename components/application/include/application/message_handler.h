#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "application/device_info.h"
#include "application/peers.h"
#include "application/queues.h"

#define PROTOCOL_MESSAGE_HANDLER_TASK_PRIORITY 3
#define PROTOCOL_MESSAGE_HANDLER_TASK_STACK_DEPTH 1024 * 4

typedef struct protocol_message_handler_t {
  app_peers_handle_t peers;
  app_queues_handle_t queues;
  app_device_info_handle_t device_info;
  struct {
    TaskHandle_t handler;
  } tasks;
} protocol_message_handler_t;

typedef protocol_message_handler_t *protocol_message_handler_handle_t;

esp_err_t protocol_message_handler_init(
    protocol_message_handler_handle_t *message_handler_ptr,
    app_peers_handle_t peers_handle, app_queues_handle_t queues_handle,
    app_device_info_handle_t device_info_handle);
