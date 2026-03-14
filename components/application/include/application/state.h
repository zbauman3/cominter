#pragma once

#include "esp_err.h"

#define APP_STATE_TASK_PRIORITY_INPUTS 4

#define APP_STATE_TASK_STACK_DEPTH_INPUTS 1024 * 2

typedef struct app_state_t {
  struct {
    char *name;
  } device_info;
} app_state_t;

typedef app_state_t *app_state_handle_t;

esp_err_t app_state_init(app_state_handle_t *state_handle_ptr);