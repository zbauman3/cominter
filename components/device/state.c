#include <string.h>

#include "device/state.h"

esp_err_t device_state_init(device_state_handle_t *device_state_handle_ptr) {
  device_state_handle_t device_state_handle =
      (device_state_handle_t)malloc(sizeof(device_state_t));
  if (device_state_handle == NULL) {
    return ESP_ERR_NO_MEM;
  }

  device_state_handle->ip_info =
      (esp_netif_ip_info_t *)malloc(sizeof(esp_netif_ip_info_t));
  if (device_state_handle->ip_info == NULL) {
    return ESP_ERR_NO_MEM;
  }

  device_state_handle->ip_info->ip = (esp_ip4_addr_t){0};
  device_state_handle->ip_info->netmask = (esp_ip4_addr_t){0};
  device_state_handle->ip_info->gw = (esp_ip4_addr_t){0};

  // Initialize device name to an empty string
  device_state_handle->device_name = (char *)malloc(sizeof(char) * 5);
  strcpy(device_state_handle->device_name, "None");

  *device_state_handle_ptr = device_state_handle;

  return ESP_OK;
}
