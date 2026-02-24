#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

#include "application/messages.h"

static const char *BASE_TAG = "APPLICATION:MESSAGES";

esp_err_t app_message_init(app_state_handle_t state_handle,
                           app_message_handle_t *message_ptr,
                           app_message_type_t type,
                           app_state_mac_address_t to_mac_address) {
  app_message_handle_t message =
      (app_message_handle_t)malloc(sizeof(app_message_t));
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
  memcpy(message->header.from_mac_address,
         state_handle->device_info.mac_address, 6);
  if (to_mac_address != NULL) {
    memcpy(message->header.to_mac_address, to_mac_address, 6);
  } else {
    memcpy(message->header.to_mac_address, APP_MESSAGE_BROADCAST_MAC_ADDRESS,
           6);
  }

  switch (type) {
  case MESSAGE_TYPE_TEXT:
    message->text.value = NULL;
    break;
  case MESSAGE_TYPE_AUDIO:
    message->audio.value = NULL;
    break;
  case MESSAGE_TYPE_HEARTBEAT:
    message->heartbeat.name = NULL;
    break;
  default:
    // no op, default to all null pointers
    message->text.value = NULL;
    message->audio.value = NULL;
    message->heartbeat.name = NULL;
    break;
  }

  *message_ptr = message;

  return ESP_OK;
}

esp_err_t app_message_init_text(app_state_handle_t state_handle,
                                app_message_handle_t *message_ptr, char *value,
                                app_state_mac_address_t to_mac_address) {
  esp_err_t ret = ESP_OK;

  ret = app_message_init(state_handle, message_ptr, MESSAGE_TYPE_TEXT,
                         to_mac_address);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->header.length = (strlen(value) + 1) * sizeof(char);
  (*message_ptr)->text.value = (char *)malloc((*message_ptr)->header.length);
  if ((*message_ptr)->text.value == NULL) {
    app_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  strcpy((*message_ptr)->text.value, value);

  return ESP_OK;
}

esp_err_t app_message_init_heartbeat(app_state_handle_t state_handle,
                                     app_message_handle_t *message_ptr) {
  esp_err_t ret = ESP_OK;

  ret =
      app_message_init(state_handle, message_ptr, MESSAGE_TYPE_HEARTBEAT, NULL);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->header.length =
      (strlen(state_handle->device_info.name) + 1) * sizeof(char);
  (*message_ptr)->heartbeat.name =
      (char *)malloc((*message_ptr)->header.length);
  if ((*message_ptr)->heartbeat.name == NULL) {
    app_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  strcpy((*message_ptr)->heartbeat.name, state_handle->device_info.name);

  return ESP_OK;
}

esp_err_t app_message_init_audio(app_state_handle_t state_handle,
                                 app_message_handle_t *message_ptr,
                                 uint8_t *value, int length,
                                 app_state_mac_address_t to_mac_address) {
  esp_err_t ret = ESP_OK;

  ret = app_message_init(state_handle, message_ptr, MESSAGE_TYPE_AUDIO,
                         to_mac_address);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->header.length = length;
  (*message_ptr)->audio.value = (uint8_t *)malloc(length);
  if ((*message_ptr)->audio.value == NULL) {
    app_message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  memcpy((*message_ptr)->audio.value, value, length);

  return ESP_OK;
}

esp_err_t app_message_set_payload(app_message_handle_t message, void *value) {
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
    free(message->heartbeat.name);

    message->heartbeat.name = (char *)malloc(message->header.length);
    if (message->heartbeat.name == NULL) {
      return ESP_ERR_NO_MEM;
    }

    strcpy(message->heartbeat.name, (char *)value);
    break;
  default:
    ESP_LOGE(BASE_TAG, "Unknown message type: %d", message->header.type);
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

void app_message_free(app_message_handle_t message) {
  switch (message->header.type) {
  case MESSAGE_TYPE_TEXT:
    free(message->text.value);
    break;
  case MESSAGE_TYPE_AUDIO:
    free(message->audio.value);
    break;
  case MESSAGE_TYPE_HEARTBEAT:
    free(message->heartbeat.name);
    break;
  default:
    // no op, free both null pointers
    // (null pointers are checked in free)
    free(message->text.value);
    free(message->audio.value);
    free(message->heartbeat.name);
    break;
  }

  free(message);
}