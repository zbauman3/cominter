#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#include "network/peers.h"

#define NETWORK_PEERS_PRUNE_INTERVAL_MS 60000 // 1 minute

esp_err_t
network_peers_init(network_peers_list_handle_t *peers_list_handle_ptr) {
  *peers_list_handle_ptr =
      (network_peers_list_handle_t)malloc(sizeof(network_peers_list_t));
  if (*peers_list_handle_ptr == NULL) {
    return ESP_ERR_NO_MEM;
  }

  (*peers_list_handle_ptr)->head = NULL;
  (*peers_list_handle_ptr)->mutex = xSemaphoreCreateMutex();
  return ESP_OK;
}

esp_err_t network_peers_add(network_peers_list_handle_t peers_list_handle,
                            network_mac_address_t mac_address, char *name) {
  xSemaphoreTake(peers_list_handle->mutex, portMAX_DELAY);

  esp_err_t ret = ESP_OK;
  network_peer_t *new_peer;

  // if the peer already exists, update the name and return
  new_peer = network_peers_find(peers_list_handle, mac_address, false);
  if (new_peer != NULL) {
    new_peer->name =
        (char *)realloc(new_peer->name, (strlen(name) + 1) * sizeof(char));
    if (new_peer->name == NULL) {
      ret = ESP_ERR_NO_MEM;
      goto network_peers_add_end;
    }

    strcpy(new_peer->name, name);
    new_peer->last_heartbeat_ms = (int32_t)(esp_timer_get_time() / 1000);

    goto network_peers_add_end;
  }

  new_peer = (network_peer_t *)malloc(sizeof(network_peer_t));
  if (new_peer == NULL) {
    ret = ESP_ERR_NO_MEM;
    goto network_peers_add_end;
  }

  new_peer->name = (char *)malloc((strlen(name) + 1) * sizeof(char));
  if (new_peer->name == NULL) {
    free(new_peer);
    ret = ESP_ERR_NO_MEM;
    goto network_peers_add_end;
  }

  memcpy(new_peer->mac_address, mac_address, sizeof(network_mac_address_t));
  strcpy(new_peer->name, name);
  new_peer->last_heartbeat_ms = (int32_t)(esp_timer_get_time() / 1000);
  new_peer->next_peer = peers_list_handle->head;
  peers_list_handle->head = new_peer;

network_peers_add_end:
  xSemaphoreGive(peers_list_handle->mutex);
  return ret;
}

esp_err_t network_peers_remove(network_peers_list_handle_t peers_list_handle,
                               network_mac_address_t mac_address) {
  xSemaphoreTake(peers_list_handle->mutex, portMAX_DELAY);

  network_peer_t *current_peer = peers_list_handle->head;
  network_peer_t *previous_peer = NULL;

  while (current_peer != NULL) {
    if (memcmp(current_peer->mac_address, mac_address,
               sizeof(network_mac_address_t)) == 0) {
      if (previous_peer == NULL) {
        peers_list_handle->head = current_peer->next_peer;
      } else {
        previous_peer->next_peer = current_peer->next_peer;
      }

      free(current_peer->name);
      free(current_peer);

      xSemaphoreGive(peers_list_handle->mutex);
      return ESP_OK;
    }
    previous_peer = current_peer;
    current_peer = current_peer->next_peer;
  }

  xSemaphoreGive(peers_list_handle->mutex);
  return ESP_ERR_NOT_FOUND;
}

esp_err_t network_peers_prune(network_peers_list_handle_t peers_list_handle) {
  xSemaphoreTake(peers_list_handle->mutex, portMAX_DELAY);

  network_peer_t *current_peer = peers_list_handle->head;
  network_peer_t *previous_peer = NULL;

  int32_t prune_threshold_ms =
      (int32_t)(esp_timer_get_time() / 1000) - NETWORK_PEERS_PRUNE_INTERVAL_MS;

  while (current_peer != NULL) {
    if (current_peer->last_heartbeat_ms < prune_threshold_ms) {
      if (previous_peer == NULL) {
        peers_list_handle->head = current_peer->next_peer;
      } else {
        previous_peer->next_peer = current_peer->next_peer;
      }

      free(current_peer->name);
      free(current_peer);

      if (previous_peer == NULL) {
        current_peer = peers_list_handle->head;
      } else {
        current_peer = previous_peer->next_peer;
      }
    } else {
      previous_peer = current_peer;
      current_peer = current_peer->next_peer;
    }
  }

  xSemaphoreGive(peers_list_handle->mutex);
  return ESP_OK;
}

network_peer_t *
network_peers_find(network_peers_list_handle_t peers_list_handle,
                   network_mac_address_t mac_address, bool should_lock) {
  if (should_lock) {
    xSemaphoreTake(peers_list_handle->mutex, portMAX_DELAY);
  }

  network_peer_t *current_peer = peers_list_handle->head;
  while (current_peer != NULL) {
    if (memcmp(current_peer->mac_address, mac_address,
               sizeof(network_mac_address_t)) == 0) {
      if (should_lock) {
        xSemaphoreGive(peers_list_handle->mutex);
      }
      return current_peer;
    }
    current_peer = current_peer->next_peer;
  }

  if (should_lock) {
    xSemaphoreGive(peers_list_handle->mutex);
  }
  return NULL;
}