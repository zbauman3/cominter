#include "esp_log.h"
#include <string.h>

#include "application/messages.h"

static const char *BASE_TAG = "APPLICATION:MESSAGES";

esp_err_t message_init(message_handle_t *message_ptr, message_type_t type) {
  message_handle_t message = (message_handle_t)malloc(sizeof(message_t));
  if (message == NULL) {
    return ESP_ERR_NO_MEM;
  }

  message->header.type = type;
  message->header.length = 0;

  switch (type) {
  case MESSAGE_TYPE_TEXT:
    message->text.value = NULL;
    break;
  case MESSAGE_TYPE_AUDIO:
    message->audio.value = NULL;
    break;
  default:
    // no op, default to both null pointers
    message->text.value = NULL;
    message->audio.value = NULL;
    break;
  }

  *message_ptr = message;

  return ESP_OK;
}

esp_err_t message_init_text(message_handle_t *message_ptr, char *value) {
  esp_err_t ret = ESP_OK;

  ret = message_init(message_ptr, MESSAGE_TYPE_TEXT);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->header.length = (strlen(value) + 1) * sizeof(char);
  (*message_ptr)->text.value = (char *)malloc((*message_ptr)->header.length);
  if ((*message_ptr)->text.value == NULL) {
    message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  strcpy((*message_ptr)->text.value, value);

  return ESP_OK;
}

esp_err_t message_init_audio(message_handle_t *message_ptr, uint8_t *value,
                             int length) {
  esp_err_t ret = ESP_OK;

  ret = message_init(message_ptr, MESSAGE_TYPE_AUDIO);
  if (ret != ESP_OK) {
    return ret;
  }

  (*message_ptr)->header.length = length;
  (*message_ptr)->audio.value = (uint8_t *)malloc(length);
  if ((*message_ptr)->audio.value == NULL) {
    message_free(*message_ptr);
    return ESP_ERR_NO_MEM;
  }

  memcpy((*message_ptr)->audio.value, value, length);

  return ESP_OK;
}

esp_err_t message_set_payload(message_handle_t message, void *value) {
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
  default:
    ESP_LOGE(BASE_TAG, "Unknown message type: %d", message->header.type);
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

void message_free(message_handle_t message) {
  switch (message->header.type) {
  case MESSAGE_TYPE_TEXT:
    free(message->text.value);
    break;
  case MESSAGE_TYPE_AUDIO:
    free(message->audio.value);
    break;
  default:
    // no op, free both null pointers
    // (null pointers are checked in free)
    free(message->text.value);
    free(message->audio.value);
    break;
  }

  free(message);
}