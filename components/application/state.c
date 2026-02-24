#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include <string.h>

#include "application/messages.h"
#include "application/state.h"

#define APP_STATE_PEERS_PRUNE_INTERVAL_MS 60000 // 1 minute

static const char *TAG = "APPLICATION:STATE";

esp_err_t app_state_init(app_state_handle_t *state_handle_ptr,
                         int talk_btn_pin) {
  app_state_handle_t state_handle =
      (app_state_handle_t)malloc(sizeof(app_state_t));
  if (state_handle == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->pins.talk_btn = talk_btn_pin;

  state_handle->network.ip_info =
      (esp_netif_ip_info_t *)malloc(sizeof(esp_netif_ip_info_t));
  if (state_handle->network.ip_info == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->network.multicast_addr_info =
      (struct addrinfo *)malloc(sizeof(struct addrinfo));
  if (state_handle->network.multicast_addr_info == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->tasks.multicast_read_task = NULL;
  state_handle->tasks.multicast_write_task = NULL;
  state_handle->tasks.socket_task = NULL;
  state_handle->tasks.inputs_task = NULL;
  state_handle->tasks.udp_heartbeat_task = NULL;
  state_handle->peers.list = NULL;
  state_handle->network.socket = -1;

  state_handle->event_groups.network_events = xEventGroupCreate();
  if (state_handle->event_groups.network_events == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->peers.peers_mutex = xSemaphoreCreateMutex();
  if (state_handle->peers.peers_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->queues.inputs_queue = xQueueCreate(10, sizeof(uint32_t));
  if (state_handle->queues.inputs_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->queues.message_outgoing_queue =
      xQueueCreate(10, sizeof(app_message_handle_t));
  if (state_handle->queues.message_outgoing_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  state_handle->network.ip_info->ip = (esp_ip4_addr_t){0};
  state_handle->network.ip_info->netmask = (esp_ip4_addr_t){0};
  state_handle->network.ip_info->gw = (esp_ip4_addr_t){0};

  if (esp_read_mac(state_handle->device_info.mac_address, ESP_MAC_WIFI_STA) !=
      ESP_OK) {
    ESP_LOGE(TAG, "Failed to read MAC address");
    return ESP_ERR_INVALID_STATE;
  };

  // init the device name to the mac address
  state_handle->device_info.name = (char *)malloc(sizeof(char) * 18);
  if (state_handle->device_info.name == NULL) {
    return ESP_ERR_NO_MEM;
  }

  snprintf(state_handle->device_info.name, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           state_handle->device_info.mac_address[0],
           state_handle->device_info.mac_address[1],
           state_handle->device_info.mac_address[2],
           state_handle->device_info.mac_address[3],
           state_handle->device_info.mac_address[4],
           state_handle->device_info.mac_address[5]);

  *state_handle_ptr = state_handle;

  return ESP_OK;
}

esp_err_t app_state_peer_add(app_state_handle_t state_handle,
                             app_state_mac_address_t mac_address, char *name) {
  xSemaphoreTake(state_handle->peers.peers_mutex, portMAX_DELAY);

  esp_err_t ret = ESP_OK;
  app_state_peer_t *new_peer;

  // if the peer already exists, update the name and return
  new_peer = app_state_peer_find(state_handle, mac_address, false);
  if (new_peer != NULL) {
    new_peer->name =
        (char *)realloc(new_peer->name, (strlen(name) + 1) * sizeof(char));
    if (new_peer->name == NULL) {
      ret = ESP_ERR_NO_MEM;
      goto app_state_peer_add_end;
    }

    strcpy(new_peer->name, name);
    new_peer->last_heartbeat_ms = (int32_t)(esp_timer_get_time() / 1000);

    goto app_state_peer_add_end;
  }

  new_peer = (app_state_peer_t *)malloc(sizeof(app_state_peer_t));
  if (new_peer == NULL) {
    ret = ESP_ERR_NO_MEM;
    goto app_state_peer_add_end;
  }

  new_peer->name = (char *)malloc((strlen(name) + 1) * sizeof(char));
  if (new_peer->name == NULL) {
    free(new_peer);
    ret = ESP_ERR_NO_MEM;
    goto app_state_peer_add_end;
  }

  memcpy(new_peer->mac_address, mac_address, 6);
  strcpy(new_peer->name, name);
  new_peer->last_heartbeat_ms = (int32_t)(esp_timer_get_time() / 1000);
  new_peer->next_peer = state_handle->peers.list;
  state_handle->peers.list = new_peer;

app_state_peer_add_end:
  xSemaphoreGive(state_handle->peers.peers_mutex);
  return ret;
}

esp_err_t app_state_peer_remove(app_state_handle_t state_handle,
                                app_state_mac_address_t mac_address) {
  xSemaphoreTake(state_handle->peers.peers_mutex, portMAX_DELAY);

  app_state_peer_t *current_peer = state_handle->peers.list;
  app_state_peer_t *previous_peer = NULL;

  while (current_peer != NULL) {
    if (memcmp(current_peer->mac_address, mac_address, 6) == 0) {
      if (previous_peer == NULL) {
        state_handle->peers.list = current_peer->next_peer;
      } else {
        previous_peer->next_peer = current_peer->next_peer;
      }

      free(current_peer->name);
      free(current_peer);

      xSemaphoreGive(state_handle->peers.peers_mutex);
      return ESP_OK;
    }
    previous_peer = current_peer;
    current_peer = current_peer->next_peer;
  }

  xSemaphoreGive(state_handle->peers.peers_mutex);
  return ESP_ERR_NOT_FOUND;
}

esp_err_t app_state_peers_prune(app_state_handle_t state_handle) {
  xSemaphoreTake(state_handle->peers.peers_mutex, portMAX_DELAY);

  app_state_peer_t *current_peer = state_handle->peers.list;
  app_state_peer_t *previous_peer = NULL;

  int32_t prune_threshold_ms = (int32_t)(esp_timer_get_time() / 1000) -
                               APP_STATE_PEERS_PRUNE_INTERVAL_MS;

  while (current_peer != NULL) {
    if (current_peer->last_heartbeat_ms < prune_threshold_ms) {
      if (previous_peer == NULL) {
        state_handle->peers.list = current_peer->next_peer;
      } else {
        previous_peer->next_peer = current_peer->next_peer;
      }

      free(current_peer->name);
      free(current_peer);

      if (previous_peer == NULL) {
        current_peer = state_handle->peers.list;
      } else {
        current_peer = previous_peer->next_peer;
      }
    } else {
      previous_peer = current_peer;
      current_peer = current_peer->next_peer;
    }
  }

  xSemaphoreGive(state_handle->peers.peers_mutex);
  return ESP_OK;
}

app_state_peer_t *app_state_peer_find(app_state_handle_t state_handle,
                                      app_state_mac_address_t mac_address,
                                      bool should_lock) {
  if (should_lock) {
    xSemaphoreTake(state_handle->peers.peers_mutex, portMAX_DELAY);
  }

  app_state_peer_t *current_peer = state_handle->peers.list;
  while (current_peer != NULL) {
    if (memcmp(current_peer->mac_address, mac_address, 6) == 0) {
      if (should_lock) {
        xSemaphoreGive(state_handle->peers.peers_mutex);
      }
      return current_peer;
    }
    current_peer = current_peer->next_peer;
  }

  if (should_lock) {
    xSemaphoreGive(state_handle->peers.peers_mutex);
  }
  return NULL;
}