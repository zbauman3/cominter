#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <string.h>

#include "network/messages.h"

static const char *BASE_TAG = "NETWORK:MESSAGES";

esp_err_t network_message_init(network_message_handle_t *message_ptr,
                               network_message_type_t type,
                               network_udp_mac_address_t from_mac_address,
                               network_udp_mac_address_t to_mac_address) {
  network_message_handle_t message =
      (network_message_handle_t)malloc(sizeof(network_message_t));
  if (message == NULL) {
    return ESP_ERR_NO_MEM;
  }

  message->header.type = type;
  message->header.length = 0;

  // uuid: 48-bit microsecond timestamp (big-endian) + 16-bit hardware RNG
  int64_t ts = esp_timer_get_time();
  uint32_t rng = esp_random();
  message->header.uuid[0] = (uint8_t)((ts >> 40) & 0xFF);
  message->header.uuid[1] = (uint8_t)((ts >> 32) & 0xFF);
  message->header.uuid[2] = (uint8_t)((ts >> 24) & 0xFF);
  message->header.uuid[3] = (uint8_t)((ts >> 16) & 0xFF);
  message->header.uuid[4] = (uint8_t)((ts >> 8) & 0xFF);
  message->header.uuid[5] = (uint8_t)(ts & 0xFF);
  message->header.uuid[6] = (uint8_t)((rng >> 8) & 0xFF);
  message->header.uuid[7] = (uint8_t)(rng & 0xFF);

  // mac addresses
  if (from_mac_address != NULL) {
    memcpy(message->header.from_mac_address, from_mac_address, 6);
  } else {
    memcpy(message->header.from_mac_address,
           NETWORK_MESSAGE_BROADCAST_MAC_ADDRESS, 6);
  }
  memcpy(message->header.from_mac_address, from_mac_address, 6);
  if (to_mac_address != NULL) {
    memcpy(message->header.to_mac_address, to_mac_address, 6);
  } else {
    memcpy(message->header.to_mac_address,
           NETWORK_MESSAGE_BROADCAST_MAC_ADDRESS, 6);
  }

  switch (type) {
  case MESSAGE_TYPE_TEXT:
    message->text.value = NULL;
    break;
  case MESSAGE_TYPE_AUDIO:
    message->audio.value = NULL;
    break;
  case MESSAGE_TYPE_HEARTBEAT:
    message->heartbeat.from_name = NULL;
    break;
  default:
    // no op, default to all null pointers
    message->text.value = NULL;
    message->audio.value = NULL;
    message->heartbeat.from_name = NULL;
    break;
  }

  *message_ptr = message;

  return ESP_OK;
}

esp_err_t network_message_init_text(network_message_handle_t *message_ptr,
                                    char *value,
                                    network_udp_mac_address_t from_mac_address,
                                    network_udp_mac_address_t to_mac_address) {
  esp_err_t ret = ESP_OK;

  ret = network_message_init(message_ptr, MESSAGE_TYPE_TEXT, from_mac_address,
                             to_mac_address);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->header.length = (strlen(value) + 1) * sizeof(char);
  (*message_ptr)->text.value = (char *)malloc((*message_ptr)->header.length);
  if ((*message_ptr)->text.value == NULL) {
    network_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  strcpy((*message_ptr)->text.value, value);

  return ESP_OK;
}

esp_err_t
network_message_init_heartbeat(network_message_handle_t *message_ptr,
                               char *from_name,
                               network_udp_mac_address_t from_mac_address) {
  esp_err_t ret = ESP_OK;

  ret = network_message_init(message_ptr, MESSAGE_TYPE_HEARTBEAT,
                             from_mac_address, NULL);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->header.length = (strlen(from_name) + 1) * sizeof(char);
  (*message_ptr)->heartbeat.from_name =
      (char *)malloc((*message_ptr)->header.length);
  if ((*message_ptr)->heartbeat.from_name == NULL) {
    network_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  strcpy((*message_ptr)->heartbeat.from_name, from_name);

  return ESP_OK;
}

esp_err_t network_message_init_audio(network_message_handle_t *message_ptr,
                                     uint8_t *value, int length,
                                     network_udp_mac_address_t from_mac_address,
                                     network_udp_mac_address_t to_mac_address) {
  esp_err_t ret = ESP_OK;

  ret = network_message_init(message_ptr, MESSAGE_TYPE_AUDIO, from_mac_address,
                             to_mac_address);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->header.length = length;
  (*message_ptr)->audio.value = (uint8_t *)malloc(length);
  if ((*message_ptr)->audio.value == NULL) {
    network_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  memcpy((*message_ptr)->audio.value, value, length);

  return ESP_OK;
}

esp_err_t network_message_set_payload(network_message_handle_t message,
                                      void *value) {
  switch (message->header.type) {
  case MESSAGE_TYPE_TEXT:
    free(message->text.value);

    message->text.value = (char *)malloc(message->header.length);
    if (message->text.value == NULL) {
      return ESP_ERR_NO_MEM;
    }

    // strcpy will copy the null terminator
    strcpy(message->text.value, (char *)value);
    break;
  case MESSAGE_TYPE_AUDIO:
    free(message->audio.value);

    message->audio.value = (uint8_t *)malloc(message->header.length);
    if (message->audio.value == NULL) {
      return ESP_ERR_NO_MEM;
    }

    memcpy(message->audio.value, value, message->header.length);
    break;
  case MESSAGE_TYPE_HEARTBEAT:
    free(message->heartbeat.from_name);

    message->heartbeat.from_name = (char *)malloc(message->header.length);
    if (message->heartbeat.from_name == NULL) {
      return ESP_ERR_NO_MEM;
    }

    strcpy(message->heartbeat.from_name, (char *)value);
    break;
  default:
    ESP_LOGE(BASE_TAG, "Unknown message type: %d", message->header.type);
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

void network_message_free(network_message_handle_t message) {
  switch (message->header.type) {
  case MESSAGE_TYPE_TEXT:
    free(message->text.value);
    break;
  case MESSAGE_TYPE_AUDIO:
    free(message->audio.value);
    break;
  case MESSAGE_TYPE_HEARTBEAT:
    free(message->heartbeat.from_name);
    break;
  default:
    // no op, free both null pointers
    // (null pointers are checked in free)
    free(message->text.value);
    free(message->audio.value);
    free(message->heartbeat.from_name);
    break;
  }

  free(message);
}