#pragma once

#include <stdint.h>

#define MESSAGE_MAX_LENGTH 256

typedef enum message_type_t {
  MESSAGE_TYPE_TEXT = (uint8_t)(0),
  MESSAGE_TYPE_AUDIO = (uint8_t)(1),
} message_type_t;

typedef struct payload_text_t {
  char *value;
} payload_text_t;

typedef struct payload_audio_t {
  uint8_t *value;
} payload_audio_t;

typedef struct message_header_t {
  message_type_t type;
  uint8_t length;
} message_header_t;

typedef struct message_t {
  message_header_t header;
  union {
    payload_text_t text;
    payload_audio_t audio;
  };
} message_t;