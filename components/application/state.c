#include "freertos/idf_additions.h"
#include <string.h>

#include "application/messages.h"
#include "application/state.h"

esp_err_t state_init(state_handle_t *state_handle_ptr, int talk_btn_pin) {
  state_handle_t state_handle = (state_handle_t)malloc(sizeof(state_t));
  if (state_handle == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->pins.talk_btn = talk_btn_pin;

  state_handle->ip_info =
      (esp_netif_ip_info_t *)malloc(sizeof(esp_netif_ip_info_t));
  if (state_handle->ip_info == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->multicast_addr_info =
      (struct addrinfo *)malloc(sizeof(struct addrinfo));
  if (state_handle->multicast_addr_info == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->task_multicast_read = NULL;
  state_handle->task_multicast_write = NULL;
  state_handle->task_socket = NULL;
  state_handle->task_inputs = NULL;
  state_handle->socket = -1;

  state_handle->network_events = xEventGroupCreate();
  if (state_handle->network_events == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->inputs_queue = xQueueCreate(10, sizeof(uint32_t));
  if (state_handle->inputs_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->message_outgoing_queue =
      xQueueCreate(10, sizeof(message_handle_t));
  if (state_handle->message_outgoing_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->ip_info->ip = (esp_ip4_addr_t){0};
  state_handle->ip_info->netmask = (esp_ip4_addr_t){0};
  state_handle->ip_info->gw = (esp_ip4_addr_t){0};

  // Initialize device name to an empty string
  state_handle->device_name = (char *)malloc(sizeof(char) * 5);
  strcpy(state_handle->device_name, "None");

  *state_handle_ptr = state_handle;

  return ESP_OK;
}
