#include "network/events.h"

esp_err_t network_events_init(network_events_handle_t *handle_ptr) {
  network_events_handle_t events_handle =
      (network_events_handle_t)malloc(sizeof(network_events_t));
  if (events_handle == NULL) {
    return ESP_ERR_NO_MEM;
  }

  events_handle->group_handle = xEventGroupCreate();
  if (events_handle->group_handle == NULL) {
    return ESP_ERR_NO_MEM;
  }

  xEventGroupClearBits(events_handle->group_handle,
                       NETWORK_EVENT_GOT_NEW_IP | NETWORK_EVENT_SOCKET_READY |
                           NETWORK_EVENT_LOST_IP);

  *handle_ptr = events_handle;

  return ESP_OK;
}