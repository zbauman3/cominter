#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#include "application/messages.h"
#include "application/peers.h"

// static const char *BASE_TAG = "APPLICATION:PEERS";
static const char *PEERS_TASK_TAG = "APPLICATION:PEERS:TASK";

void app_peers_heartbeat_task(void *pvParameters) {
  app_peers_handle_t app_peers_handle = (app_peers_handle_t)pvParameters;
  BaseType_t xReturned;
  app_message_handle_t outgoing_message;

  // when first coming on the network, send 10 heartbeats to make sure we're
  // put into peer lists.
  //
  // ---------------------------------------------------------------------------
  // Need to work on this. Ideally this system would work:
  // - Every time the network comes online, send 6 heartbeats.
  // - Every time we receive a heartbeat from a peer, send 3 in response.
  // - Every time we reconnect to the network, send 6 heartbeats.
  //
  // Also, this should wait until the network is ready before sending
  // heartbeats, otherwise this will just send all heartbeats immediately.
  // ---------------------------------------------------------------------------
  uint8_t init_heartbeat_count = 10;

  while (true) {
    if (app_message_init_heartbeat(
            &outgoing_message, app_peers_handle->device_info->name,
            app_peers_handle->device_info->mac_address) != ESP_OK) {
      ESP_LOGE(PEERS_TASK_TAG, "Failed to initialize message");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    xReturned = xQueueSendToBack(
        app_peers_handle->queues->message_outgoing, &outgoing_message,
        pdMS_TO_TICKS(APP_PEERS_HEARTBEAT_WAIT_MAX_MS));
    if (xReturned != pdPASS) {
      ESP_LOGE(PEERS_TASK_TAG,
               "Failed to send message to queue. Dropping message.");
      app_message_free(outgoing_message);
    }

    // the queue will now own the message.
    outgoing_message = NULL;

    // prune while we're here
    app_peers_prune(app_peers_handle);

    if (init_heartbeat_count > 0) {
      init_heartbeat_count--;
      vTaskDelay(pdMS_TO_TICKS(APP_PEERS_HEARTBEAT_INIT_INTERVAL_MS));
    } else {
      vTaskDelay(pdMS_TO_TICKS(APP_PEERS_HEARTBEAT_INTERVAL_MS));
    }
  }
}

esp_err_t app_peers_init(app_peers_handle_t *peers_handle_ptr,
                         app_device_info_handle_t device_info_handle,
                         app_queues_handle_t queues_handle) {
  app_peers_handle_t app_peers_handle =
      (app_peers_handle_t)malloc(sizeof(app_peers_t));
  if (app_peers_handle == NULL) {
    return ESP_ERR_NO_MEM;
  }

  app_peers_handle->device_info = device_info_handle;
  app_peers_handle->queues = queues_handle;
  app_peers_handle->list.head = NULL;
  app_peers_handle->list.mutex = xSemaphoreCreateMutex();
  if (app_peers_handle->list.mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  app_peers_handle->tasks.heartbeat = NULL;
  if (xTaskCreate(app_peers_heartbeat_task, PEERS_TASK_TAG,
                  APP_PEERS_TASK_STACK_DEPTH_HEARTBEAT, app_peers_handle,
                  APP_PEERS_TASK_PRIORITY_HEARTBEAT,
                  &app_peers_handle->tasks.heartbeat) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  if (app_peers_handle->tasks.heartbeat == NULL) {
    return ESP_ERR_NO_MEM;
  }

  *peers_handle_ptr = app_peers_handle;

  return ESP_OK;
}

esp_err_t app_peers_add(app_peers_handle_t peers_handle,
                        network_mac_address_t mac_address, char *name) {
  xSemaphoreTake(peers_handle->list.mutex, portMAX_DELAY);

  esp_err_t ret = ESP_OK;
  app_peer_handle_t new_peer = NULL;

  // if the peer already exists, remove it first.
  app_peers_find(peers_handle, &new_peer, mac_address, false);
  if (new_peer != NULL) {
    app_peer_free(new_peer);
    ret = app_peers_remove(peers_handle, mac_address, false);
    if (ret != ESP_OK) {
      goto app_peers_add_end;
    }
  }

  new_peer = NULL;
  new_peer = (app_peer_handle_t)malloc(sizeof(app_peer_t));
  if (new_peer == NULL) {
    ret = ESP_ERR_NO_MEM;
    goto app_peers_add_end;
  }

  new_peer->name = (char *)malloc((strlen(name) + 1) * sizeof(char));
  if (new_peer->name == NULL) {
    free(new_peer);
    ret = ESP_ERR_NO_MEM;
    goto app_peers_add_end;
  }

  memcpy(new_peer->mac_address, mac_address, sizeof(network_mac_address_t));
  strcpy(new_peer->name, name);
  new_peer->last_heartbeat_ms = (int32_t)(esp_timer_get_time() / 1000);
  new_peer->next_peer = peers_handle->list.head;
  peers_handle->list.head = new_peer;

app_peers_add_end:
  xSemaphoreGive(peers_handle->list.mutex);
  return ret;
}

esp_err_t app_peers_remove(app_peers_handle_t peers_handle,
                           network_mac_address_t mac_address,
                           bool should_lock) {
  if (should_lock) {
    xSemaphoreTake(peers_handle->list.mutex, portMAX_DELAY);
  }

  app_peer_handle_t current_peer = peers_handle->list.head;
  app_peer_handle_t previous_peer = NULL;

  while (current_peer != NULL) {
    if (memcmp(current_peer->mac_address, mac_address,
               sizeof(network_mac_address_t)) == 0) {
      if (previous_peer == NULL) {
        peers_handle->list.head = current_peer->next_peer;
      } else {
        previous_peer->next_peer = current_peer->next_peer;
      }

      free(current_peer->name);
      free(current_peer);

      if (should_lock) {
        xSemaphoreGive(peers_handle->list.mutex);
      }
      return ESP_OK;
    }
    previous_peer = current_peer;
    current_peer = current_peer->next_peer;
  }

  if (should_lock) {
    xSemaphoreGive(peers_handle->list.mutex);
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t app_peers_prune(app_peers_handle_t peers_handle) {
  xSemaphoreTake(peers_handle->list.mutex, portMAX_DELAY);

  app_peer_handle_t current_peer = peers_handle->list.head;
  app_peer_handle_t previous_peer = NULL;
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

  while (current_peer != NULL) {
    uint32_t elapsed = now_ms - current_peer->last_heartbeat_ms;
    if (elapsed > APP_PEERS_PRUNE_INTERVAL_MS) {
      if (previous_peer == NULL) {
        peers_handle->list.head = current_peer->next_peer;
      } else {
        previous_peer->next_peer = current_peer->next_peer;
      }

      free(current_peer->name);
      free(current_peer);

      if (previous_peer == NULL) {
        current_peer = peers_handle->list.head;
      } else {
        current_peer = previous_peer->next_peer;
      }
    } else {
      previous_peer = current_peer;
      current_peer = current_peer->next_peer;
    }
  }

  xSemaphoreGive(peers_handle->list.mutex);
  return ESP_OK;
}

void app_peers_find(app_peers_handle_t peers_handle,
                    app_peer_handle_t *peer_handle_ptr,
                    network_mac_address_t mac_address, bool should_lock) {
  if (should_lock) {
    xSemaphoreTake(peers_handle->list.mutex, portMAX_DELAY);
  }

  *peer_handle_ptr = NULL;
  app_peer_handle_t current_peer = peers_handle->list.head;

  while (current_peer != NULL) {
    if (memcmp(current_peer->mac_address, mac_address,
               sizeof(network_mac_address_t)) == 0) {
      // found the peer. Copy it.
      *peer_handle_ptr = (app_peer_handle_t)malloc(sizeof(app_peer_t));
      if (*peer_handle_ptr == NULL) {
        ESP_LOGE(PEERS_TASK_TAG, "Failed to allocate memory for peer");
        if (should_lock) {
          xSemaphoreGive(peers_handle->list.mutex);
        }
        return;
      }

      memcpy(*peer_handle_ptr, current_peer, sizeof(app_peer_t));
      (*peer_handle_ptr)->name = strdup(current_peer->name);
      if ((*peer_handle_ptr)->name == NULL) {
        ESP_LOGE(PEERS_TASK_TAG, "Failed to allocate memory for peer name");
        free(*peer_handle_ptr);
        *peer_handle_ptr = NULL;
      }

      if (should_lock) {
        xSemaphoreGive(peers_handle->list.mutex);
      }
      return;
    }

    current_peer = current_peer->next_peer;
  }

  if (should_lock) {
    xSemaphoreGive(peers_handle->list.mutex);
  }
  return;
}

int app_peers_count(app_peers_handle_t peers_handle) {
  xSemaphoreTake(peers_handle->list.mutex, portMAX_DELAY);

  int count = 0;
  app_peer_handle_t current_peer = peers_handle->list.head;
  while (current_peer != NULL) {
    count++;
    current_peer = current_peer->next_peer;
  }

  xSemaphoreGive(peers_handle->list.mutex);
  return count;
}

void app_peer_free(app_peer_handle_t peer_handle) {
  if (peer_handle == NULL) {
    return;
  }

  free(peer_handle->name);
  free(peer_handle);
}