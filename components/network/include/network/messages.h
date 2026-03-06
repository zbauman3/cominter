#pragma once

#include "esp_err.h"

#include "network/types.h"

// Looks like ESP-IDF's socket implementation doesn't support the flag for
// "Don't Fragment". But either way this is a reasonable default.
// Riding on the back of giants with the same max as QUIC.
// https://datatracker.ietf.org/doc/html/rfc9000#name-datagram-size
#define NETWORK_MESSAGE_MAX_LENGTH 1200

typedef enum network_message_type_t {
  MESSAGE_TYPE_UNKNOWN = 0,
  MESSAGE_TYPE_HEARTBEAT = 1,
  MESSAGE_TYPE_TEXT = 2,
  MESSAGE_TYPE_AUDIO = 3,
} network_message_type_t;

typedef struct network_message_payload_text_t {
  char *value;
} network_message_payload_text_t;

typedef struct network_message_payload_audio_t {
  uint8_t *value;
} network_message_payload_audio_t;

typedef struct network_message_payload_heartbeat_t {
  char *from_name;
} network_message_payload_heartbeat_t;

// 8-byte unique message ID: 6 bytes of microsecond timestamp + 2 bytes of
// hardware RNG. Globally unique in combination with from_mac_address.
typedef uint8_t network_message_uuid_t[8];

typedef struct network_message_header_t {
  network_message_type_t type;
  int length;
  network_message_uuid_t uuid;
  network_mac_address_t from_mac_address;
  network_mac_address_t to_mac_address;
} network_message_header_t;

typedef struct network_message_t {
  network_message_header_t header;
  union {
    network_message_payload_text_t text;
    network_message_payload_audio_t audio;
    network_message_payload_heartbeat_t heartbeat;
  };
} network_message_t;

typedef network_message_t *network_message_handle_t;

esp_err_t network_message_init(network_message_handle_t *message_ptr,
                               network_message_type_t type,
                               network_mac_address_t from_mac_address,
                               network_mac_address_t to_mac_address);
esp_err_t network_message_init_text(network_message_handle_t *message_ptr,
                                    char *value,
                                    network_mac_address_t from_mac_address,
                                    network_mac_address_t to_mac_address);
esp_err_t
network_message_init_heartbeat(network_message_handle_t *message_ptr,
                               char *from_name,
                               network_mac_address_t from_mac_address);
esp_err_t network_message_init_audio(network_message_handle_t *message_ptr,
                                     uint8_t *value, int length,
                                     network_mac_address_t from_mac_address,
                                     network_mac_address_t to_mac_address);

// it is expected that the message header length is already set
esp_err_t network_message_set_payload(network_message_handle_t message,
                                      void *value);

void network_message_free(network_message_handle_t message);