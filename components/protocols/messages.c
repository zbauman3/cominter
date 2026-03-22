#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <assert.h>
#include <string.h>

#include "protocols/messages.h"

static const char *BASE_TAG = "NETWORK:MESSAGES";

// if the to mac address is not provided, it will be set to the
// broadcast address.
esp_err_t protocol_message_init(protocol_message_handle_t *message_ptr,
                                protocol_message_type_t type, int32_t length,
                                protocol_mac_address_t from_mac_address,
                                protocol_mac_address_t to_mac_address) {

  // from mac address is required
  if (from_mac_address == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // check that the input length doesn't exceed the allowed length
  if (length < 0 || length > PROTOCOL_MESSAGE_BODY_MAX_LENGTH) {
    return ESP_ERR_INVALID_ARG;
  }

  protocol_message_handle_t message =
      (protocol_message_handle_t)malloc(sizeof(protocol_message_t));
  if (message == NULL) {
    return ESP_ERR_NO_MEM;
  }

  message->header.type = type;
  message->header.length = length;

  // uuid: 48-bit microsecond timestamp + 16-bit hardware RNG
  // stored in big-endian for network byte order
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

  memcpy(message->header.from_mac_address, from_mac_address,
         sizeof(protocol_mac_address_t));

  if (to_mac_address != NULL) {
    memcpy(message->header.to_mac_address, to_mac_address,
           sizeof(protocol_mac_address_t));
  } else {
    memcpy(message->header.to_mac_address,
           NETWORK_MESSAGE_BROADCAST_MAC_ADDRESS,
           sizeof(protocol_mac_address_t));
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
    break;
  }

  *message_ptr = message;

  return ESP_OK;
}

esp_err_t protocol_message_init_text(protocol_message_handle_t *message_ptr,
                                     char *value,
                                     protocol_mac_address_t from_mac_address,
                                     protocol_mac_address_t to_mac_address) {
  esp_err_t ret = ESP_OK;

  int32_t length = (strlen(value) + 1) * sizeof(char);

  ret = protocol_message_init(message_ptr, MESSAGE_TYPE_TEXT, length,
                              from_mac_address, to_mac_address);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->text.value = (char *)malloc((*message_ptr)->header.length);
  if ((*message_ptr)->text.value == NULL) {
    protocol_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  strcpy((*message_ptr)->text.value, value);

  return ESP_OK;
}

esp_err_t
protocol_message_init_heartbeat(protocol_message_handle_t *message_ptr,
                                char *from_name,
                                protocol_mac_address_t from_mac_address) {
  esp_err_t ret = ESP_OK;

  int32_t length = (strlen(from_name) + 1) * sizeof(char);
  ret = protocol_message_init(message_ptr, MESSAGE_TYPE_HEARTBEAT, length,
                              from_mac_address, NULL);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->heartbeat.from_name =
      (char *)malloc((*message_ptr)->header.length);
  if ((*message_ptr)->heartbeat.from_name == NULL) {
    protocol_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  strcpy((*message_ptr)->heartbeat.from_name, from_name);

  return ESP_OK;
}

esp_err_t protocol_message_init_audio(protocol_message_handle_t *message_ptr,
                                      uint8_t *value, int32_t length,
                                      protocol_mac_address_t from_mac_address,
                                      protocol_mac_address_t to_mac_address) {
  esp_err_t ret = ESP_OK;

  ret = protocol_message_init(message_ptr, MESSAGE_TYPE_AUDIO, length,
                              from_mac_address, to_mac_address);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->audio.value = (uint8_t *)malloc(length);
  if ((*message_ptr)->audio.value == NULL) {
    protocol_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  memcpy((*message_ptr)->audio.value, value, length);

  return ESP_OK;
}

esp_err_t protocol_message_set_payload(protocol_message_handle_t message,
                                       void *value) {
  switch (message->header.type) {
  case MESSAGE_TYPE_TEXT:
    free(message->text.value);

    message->text.value = (char *)malloc(message->header.length);
    if (message->text.value == NULL) {
      return ESP_ERR_NO_MEM;
    }

    // `message->header.length` accounts for the null terminator
    memcpy(message->text.value, (char *)value, message->header.length);
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

    memcpy(message->heartbeat.from_name, (char *)value, message->header.length);
    break;
  default:
    ESP_LOGE(BASE_TAG, "Unknown message type: %d", message->header.type);
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

void protocol_message_free(protocol_message_handle_t message) {
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
    // no op, free all null pointers
    // (null pointers are checked in free)
    free(message->text.value);
    break;
  }

  free(message);
}