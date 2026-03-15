#include "application/queues.h"
#include "application/messages.h"

// static const char *BASE_TAG = "NETWORK:QUEUES";

esp_err_t app_queues_init(app_queues_handle_t *handle_ptr) {
  app_queues_handle_t app_queues_handle =
      (app_queues_handle_t)malloc(sizeof(app_queues_t));
  if (app_queues_handle == NULL) {
    return ESP_ERR_NO_MEM;
  }

  app_queues_handle->message_outgoing =
      xQueueCreate(10, sizeof(app_message_handle_t));
  if (app_queues_handle->message_outgoing == NULL) {
    return ESP_ERR_NO_MEM;
  }

  app_queues_handle->message_incoming =
      xQueueCreate(10, sizeof(app_message_handle_t));
  if (app_queues_handle->message_incoming == NULL) {
    return ESP_ERR_NO_MEM;
  }

  *handle_ptr = app_queues_handle;

  return ESP_OK;
}