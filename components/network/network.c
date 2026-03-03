#include "network.h"

esp_err_t network_init(network_handle_t *network_handle_ptr) {
  *network_handle_ptr = (network_handle_t)malloc(sizeof(network_t));
  if (*network_handle_ptr == NULL) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t ret = ESP_OK;
  ret = network_peers_init(&(*network_handle_ptr)->peers_list);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = network_wifi_init(&(*network_handle_ptr)->wifi);
  if (ret != ESP_OK) {
    return ret;
  }

  return ret;
}

void network_free(network_handle_t network_handle) {
  network_peers_free(network_handle->peers_list);
  network_wifi_free(network_handle->wifi);
  free(network_handle);
}