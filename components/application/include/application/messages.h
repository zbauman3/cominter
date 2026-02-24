#pragma once

#include "esp_err.h"
#include <stdint.h>

#include "application/state.h"

// Looks like ESP-IDF's socket implementation doesn't support the flag for
// "Don't Fragment". But either way this is a reasonable default.
// Riding on the back of giants with the same max as QUIC.
// https://datatracker.ietf.org/doc/html/rfc9000#name-datagram-size
#define APP_MESSAGE_MAX_LENGTH 1200

static const app_state_mac_address_t APP_MESSAGE_BROADCAST_MAC_ADDRESS = {
    255, 255, 255, 255, 255, 255};

typedef enum app_message_type_t {
  MESSAGE_TYPE_UNKNOWN = 0,
  MESSAGE_TYPE_HEARTBEAT = 1,
  MESSAGE_TYPE_TEXT = 2,
  MESSAGE_TYPE_AUDIO = 3,
} app_message_type_t;

typedef struct app_message_payload_text_t {
  char *value;
} app_message_payload_text_t;

typedef struct app_message_payload_audio_t {
  uint8_t *value;
} app_message_payload_audio_t;

typedef struct app_message_payload_heartbeat_t {
  char *name;
} app_message_payload_heartbeat_t;

// 8-byte unique message ID: 6 bytes of microsecond timestamp + 2 bytes of
// hardware RNG. Globally unique in combination with from_mac_address.
typedef uint8_t app_message_uuid_t[8];

typedef struct app_message_header_t {
  app_message_type_t type;
  int length;
  app_message_uuid_t uuid;
  uint8_t from_mac_address[6];
  uint8_t to_mac_address[6];
} app_message_header_t;

typedef struct app_message_t {
  app_message_header_t header;
  union {
    app_message_payload_text_t text;
    app_message_payload_audio_t audio;
    app_message_payload_heartbeat_t heartbeat;
  };
} app_message_t;

typedef app_message_t *app_message_handle_t;

esp_err_t app_message_init(app_state_handle_t state_handle,
                           app_message_handle_t *message_ptr,
                           app_message_type_t type,
                           app_state_mac_address_t to_mac_address);
esp_err_t app_message_init_text(app_state_handle_t state_handle,
                                app_message_handle_t *message_ptr, char *value,
                                app_state_mac_address_t to_mac_address);
esp_err_t app_message_init_heartbeat(app_state_handle_t state_handle,
                                     app_message_handle_t *message_ptr);
esp_err_t app_message_init_audio(app_state_handle_t state_handle,
                                 app_message_handle_t *message_ptr,
                                 uint8_t *value, int length,
                                 app_state_mac_address_t to_mac_address);

// it is expected that the message header length is already set
esp_err_t app_message_set_payload(app_message_handle_t message, void *value);

void app_message_free(app_message_handle_t message);