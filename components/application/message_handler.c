#include "esp_log.h"
#include <string.h>

#include "application/message_handler.h"
#include "application/messages.h"

static const char *MESSAGE_HANDLER_TAG = "APPLICATION:MESSAGE_HANDLER";

void app_message_handler_task(void *pvParameters) {
  app_message_handler_handle_t message_handler =
      (app_message_handler_handle_t)pvParameters;
  app_message_handle_t message_incoming = NULL;
  app_peer_handle_t peer_handle = NULL;

  while (1) {
    xQueueReceive(message_handler->queues->message_incoming, &message_incoming,
                  portMAX_DELAY);

    ESP_LOGI(
        MESSAGE_HANDLER_TAG, "UUID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        message_incoming->header.uuid[0], message_incoming->header.uuid[1],
        message_incoming->header.uuid[2], message_incoming->header.uuid[3],
        message_incoming->header.uuid[4], message_incoming->header.uuid[5],
        message_incoming->header.uuid[6], message_incoming->header.uuid[7]);

    // check if the `to_mac_address` is the same as the local MAC address
    // of if it is the broadcast MAC address.
    if (memcmp(message_incoming->header.to_mac_address,
               message_handler->device_info->mac_address,
               sizeof(network_mac_address_t)) != 0 &&
        memcmp(message_incoming->header.to_mac_address,
               NETWORK_MESSAGE_BROADCAST_MAC_ADDRESS,
               sizeof(network_mac_address_t)) != 0) {
      ESP_LOGI(MESSAGE_HANDLER_TAG, "Message is not for me, skipping");
      app_message_free(message_incoming);
      message_incoming = NULL;
      continue;
    }

    // find/log the peers name if they exist
    app_peers_find(message_handler->peers, &peer_handle,
                   message_incoming->header.from_mac_address, true);
    if (peer_handle != NULL) {
      ESP_LOGI(MESSAGE_HANDLER_TAG, "Peer name: %s", peer_handle->name);
    } else {
      ESP_LOGI(MESSAGE_HANDLER_TAG, "Peer not found");
    }
    app_peer_free(peer_handle);
    peer_handle = NULL;

    switch (message_incoming->header.type) {
    case MESSAGE_TYPE_TEXT:
      ESP_LOGI(MESSAGE_HANDLER_TAG, "%s\n", message_incoming->text.value);
      break;
    case MESSAGE_TYPE_AUDIO:
      ESP_LOGI(MESSAGE_HANDLER_TAG, "Audio message of length %d received\n",
               message_incoming->header.length);
      break;
    case MESSAGE_TYPE_HEARTBEAT:
      ESP_LOGI(MESSAGE_HANDLER_TAG, "Heartbeat");
      app_peers_add(message_handler->peers,
                    message_incoming->header.from_mac_address,
                    message_incoming->heartbeat.from_name);
      ESP_LOGI(MESSAGE_HANDLER_TAG, "Number of peers: %d\n",
               app_peers_count(message_handler->peers));
      break;
    default:
      ESP_LOGE(MESSAGE_HANDLER_TAG, "Unknown message type: %d\n",
               message_incoming->header.type);
      break;
    }

    app_message_free(message_incoming);
    message_incoming = NULL;
  }
}

esp_err_t
app_message_handler_init(app_message_handler_handle_t *message_handler_ptr,
                         app_peers_handle_t peers_handle,
                         app_queues_handle_t queues_handle,
                         app_device_info_handle_t device_info_handle) {
  app_message_handler_handle_t message_handler =
      (app_message_handler_handle_t)malloc(sizeof(app_message_handler_t));
  if (message_handler == NULL) {
    return ESP_ERR_NO_MEM;
  }

  message_handler->peers = peers_handle;
  message_handler->queues = queues_handle;
  message_handler->device_info = device_info_handle;

  message_handler->tasks.handler = NULL;
  if (xTaskCreate(app_message_handler_task, MESSAGE_HANDLER_TAG,
                  APP_MESSAGE_HANDLER_TASK_STACK_DEPTH, message_handler,
                  APP_MESSAGE_HANDLER_TASK_PRIORITY,
                  &message_handler->tasks.handler) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  if (message_handler->tasks.handler == NULL) {
    return ESP_ERR_NO_MEM;
  }

  *message_handler_ptr = message_handler;

  return ESP_OK;
}