#pragma once

#include "esp_err.h"

#include "protocols/mac.h"

// Looks like ESP-IDF's socket implementation doesn't support the flag for
// "Don't Fragment". But either way this is a reasonable default.
// Riding on the back of giants with the same max as QUIC.
// https://datatracker.ietf.org/doc/html/rfc9000#name-datagram-size
#define PROTOCOL_MESSAGE_MAX_LENGTH 1200
#define PROTOCOL_MESSAGE_BODY_MAX_LENGTH                                       \
  (PROTOCOL_MESSAGE_MAX_LENGTH - sizeof(protocol_message_header_t))

typedef enum protocol_message_type_t {
  MESSAGE_TYPE_UNKNOWN = 0,
  MESSAGE_TYPE_HEARTBEAT = 1,
  MESSAGE_TYPE_TEXT = 2,
  MESSAGE_TYPE_AUDIO = 3,
} protocol_message_type_t;

typedef struct protocol_message_payload_text_t {
  char *value;
} protocol_message_payload_text_t;

typedef struct protocol_message_payload_audio_t {
  uint8_t *value;
} protocol_message_payload_audio_t;

typedef struct protocol_message_payload_heartbeat_t {
  char *from_name;
} protocol_message_payload_heartbeat_t;

// 8-byte unique message ID: 6 bytes of microsecond timestamp + 2 bytes of
// hardware RNG. Globally unique in combination with from_mac_address.
typedef uint8_t protocol_message_uuid_t[8];

typedef struct protocol_message_header_t {
  protocol_message_type_t type;
  int32_t length;
  protocol_message_uuid_t uuid;
  protocol_mac_address_t from_mac_address;
  protocol_mac_address_t to_mac_address;
} protocol_message_header_t;

typedef struct protocol_message_t {
  protocol_message_header_t header;
  union {
    protocol_message_payload_text_t text;
    protocol_message_payload_audio_t audio;
    protocol_message_payload_heartbeat_t heartbeat;
  };
} protocol_message_t;

typedef protocol_message_t *protocol_message_handle_t;

esp_err_t protocol_message_init(protocol_message_handle_t *message_ptr,
                                protocol_message_type_t type, int32_t length,
                                protocol_mac_address_t from_mac_address,
                                protocol_mac_address_t to_mac_address);
esp_err_t protocol_message_init_text(protocol_message_handle_t *message_ptr,
                                     char *value,
                                     protocol_mac_address_t from_mac_address,
                                     protocol_mac_address_t to_mac_address);
esp_err_t
protocol_message_init_heartbeat(protocol_message_handle_t *message_ptr,
                                char *from_name,
                                protocol_mac_address_t from_mac_address);
esp_err_t protocol_message_init_audio(protocol_message_handle_t *message_ptr,
                                      uint8_t *value, int32_t length,
                                      protocol_mac_address_t from_mac_address,
                                      protocol_mac_address_t to_mac_address);

// it is expected that the message header length is already set
esp_err_t protocol_message_set_payload(protocol_message_handle_t message,
                                       void *value);

void protocol_message_free(protocol_message_handle_t message);