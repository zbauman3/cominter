#include "esp_err.h"
#include "esp_log.h"

#include "application/messages.h"
#include "application/queues.h"

static const char *BASE_TAG = "NETWORK:QUEUES";

esp_err_t app_queues_init(app_queues_handle_t *handle_ptr) {
  app_queues_handle_t app_queues_handle =
      (app_queues_handle_t)malloc(sizeof(app_queues_t));
  if (app_queues_handle == NULL) {
    return ESP_ERR_NO_MEM;
  }

  app_queues_handle->outgoing = xQueueCreate(10, sizeof(app_message_handle_t));
  if (app_queues_handle->outgoing == NULL) {
    return ESP_ERR_NO_MEM;
  }

  app_queues_handle->incoming_heartbeat =
      xQueueCreate(10, sizeof(app_message_handle_t));
  if (app_queues_handle->incoming_heartbeat == NULL) {
    return ESP_ERR_NO_MEM;
  }

  app_queues_handle->incoming_text =
      xQueueCreate(10, sizeof(app_message_handle_t));
  if (app_queues_handle->incoming_text == NULL) {
    return ESP_ERR_NO_MEM;
  }

  *handle_ptr = app_queues_handle;

  return ESP_OK;
}

// If successful, the caller will own the message and is responsible for freeing
// it. Otherwise, the caller's pointer will be set to NULL and they can choose
// to retry or not.
esp_err_t app_queues_receive_outgoing_message(app_queues_handle_t queues_handle,
                                              app_message_handle_t *message_ptr,
                                              TickType_t ticks_to_wait) {
  BaseType_t xReturned =
      xQueueReceive(queues_handle->outgoing, message_ptr, ticks_to_wait);
  if (xReturned != pdPASS) {
    *message_ptr = NULL;
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}

// If successful, the queue will own the message and the caller's pointer will
// be set to NULL. Otherwise, the caller's pointer will not be modified and they
// can choose to retry or not.
esp_err_t app_queues_add_outgoing_message(app_queues_handle_t queues_handle,
                                          app_message_handle_t *message_ptr,
                                          TickType_t ticks_to_wait,
                                          bool should_send_to_front) {
  BaseType_t xReturned = pdPASS;
  if (should_send_to_front) {
    xReturned =
        xQueueSendToFront(queues_handle->outgoing, message_ptr, ticks_to_wait);
  } else {
    xReturned =
        xQueueSendToBack(queues_handle->outgoing, message_ptr, ticks_to_wait);
  }

  if (xReturned != pdPASS) {
    return ESP_ERR_TIMEOUT;
  }

  *message_ptr = NULL;
  return ESP_OK;
}

// If successful, the caller will own the message and is responsible for freeing
// it. Otherwise, the caller's pointer will be set to NULL and they can choose
// to retry or not.
esp_err_t app_queues_receive_incoming_message(app_queues_handle_t queues_handle,
                                              app_message_handle_t *message_ptr,
                                              app_message_type_t type,
                                              TickType_t ticks_to_wait) {
  QueueHandle_t queue_to_receive_from = NULL;
  switch (type) {
  case MESSAGE_TYPE_HEARTBEAT:
    queue_to_receive_from = queues_handle->incoming_heartbeat;
    break;
  case MESSAGE_TYPE_TEXT:
    queue_to_receive_from = queues_handle->incoming_text;
    break;
  default:
    ESP_LOGE(BASE_TAG, "Unsupported message type: %d", type);
    *message_ptr = NULL;
    return ESP_ERR_INVALID_ARG;
  }

  BaseType_t xReturned =
      xQueueReceive(queue_to_receive_from, message_ptr, ticks_to_wait);
  if (xReturned != pdPASS) {
    *message_ptr = NULL;
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}

// If successful, the queue will own the message and the caller's pointer will
// be set to NULL. Otherwise, the caller's pointer will not be modified and they
// can choose to retry or not.
esp_err_t app_queues_add_incoming_message(app_queues_handle_t queues_handle,
                                          app_message_handle_t *message_ptr,
                                          TickType_t ticks_to_wait) {
  BaseType_t xReturned = pdPASS;
  switch ((*message_ptr)->header.type) {
  case MESSAGE_TYPE_HEARTBEAT:
    xReturned = xQueueSendToBack(queues_handle->incoming_heartbeat, message_ptr,
                                 ticks_to_wait);
    break;
  case MESSAGE_TYPE_TEXT:
    xReturned = xQueueSendToBack(queues_handle->incoming_text, message_ptr,
                                 ticks_to_wait);
    break;
  default:
    xReturned = pdPASS;
    ESP_LOGE(BASE_TAG, "Unsupported message type: %d",
             (*message_ptr)->header.type);
    app_message_free(*message_ptr);
    break;
  }

  if (xReturned != pdPASS) {
    return ESP_ERR_TIMEOUT;
  }

  *message_ptr = NULL;
  return ESP_OK;
}