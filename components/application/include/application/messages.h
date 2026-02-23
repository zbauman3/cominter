#pragma once

#include "esp_err.h"
#include <stdint.h>

#include "application/state.h"

// Looks like ESP-IDF's socket implementation doesn't support the flag for
// "Don't Fragment". But either way this is a reasonable default.
// Riding on the back of giants with the same max as QUIC.
// https://datatracker.ietf.org/doc/html/rfc9000#name-datagram-size
#define MESSAGE_MAX_LENGTH 1200

typedef enum message_type_t {
  MESSAGE_TYPE_UNKNOWN = 0,
  MESSAGE_TYPE_TEXT = 1,
  MESSAGE_TYPE_AUDIO = 2,
} message_type_t;

typedef struct payload_text_t {
  char *value;
} payload_text_t;

typedef struct payload_audio_t {
  uint8_t *value;
} payload_audio_t;

typedef struct message_header_t {
  message_type_t type;
  int length;
  uint8_t sender_mac_address[6];
} message_header_t;

typedef struct message_t {
  message_header_t header;
  union {
    payload_text_t text;
    payload_audio_t audio;
  };
} message_t;

typedef message_t *message_handle_t;

esp_err_t message_init(state_handle_t state_handle,
                       message_handle_t *message_ptr, message_type_t type);
esp_err_t message_init_text(state_handle_t state_handle,
                            message_handle_t *message_ptr, char *value);
esp_err_t message_init_audio(state_handle_t state_handle,
                             message_handle_t *message_ptr, uint8_t *value,
                             int length);

// it is expected that the message header length is already set
esp_err_t message_set_payload(message_handle_t message, void *value);

void message_free(message_handle_t message);