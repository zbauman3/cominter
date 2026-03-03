#include "network/events.h"

esp_err_t network_events_init(network_events_handle_t *handle_ptr) {
  *handle_ptr = (network_events_handle_t)malloc(sizeof(network_events_t));
  if (*handle_ptr == NULL) {
    return ESP_ERR_NO_MEM;
  }

  (*handle_ptr)->group_handle = xEventGroupCreate();
  xEventGroupClearBits((*handle_ptr)->group_handle,
                       NETWORK_EVENT_GOT_NEW_IP | NETWORK_EVENT_SOCKET_READY);

  return ESP_OK;
}

void network_events_free(network_events_handle_t handle) {
  vEventGroupDelete(handle->group_handle);
  free(handle);
}