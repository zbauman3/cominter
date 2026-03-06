#include "network/queues.h"
#include "network/messages.h"

static const char *BASE_TAG = "NETWORK:QUEUES";

esp_err_t network_queues_init(network_queues_handle_t *handle_ptr) {
  *handle_ptr = (network_queues_handle_t)malloc(sizeof(network_queues_t));
  if (*handle_ptr == NULL) {
    return ESP_ERR_NO_MEM;
  }

  (*handle_ptr)->message_outgoing =
      xQueueCreate(10, sizeof(network_message_handle_t));
  if ((*handle_ptr)->message_outgoing == NULL) {
    return ESP_ERR_NO_MEM;
  }

  (*handle_ptr)->message_incoming =
      xQueueCreate(10, sizeof(network_message_handle_t));
  if ((*handle_ptr)->message_incoming == NULL) {
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void network_queues_free(network_queues_handle_t handle) {
  vQueueDelete(handle->message_outgoing);
  vQueueDelete(handle->message_incoming);
  free(handle);
}