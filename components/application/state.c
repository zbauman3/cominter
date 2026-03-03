#include "esp_log.h"
#include <string.h>

#include "application/state.h"

static const char *TAG = "APPLICATION:STATE";

esp_err_t app_state_init(app_state_handle_t *state_handle_ptr) {
  app_state_handle_t state_handle =
      (app_state_handle_t)malloc(sizeof(app_state_t));
  if (state_handle == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for state handle");
    return ESP_ERR_NO_MEM;
  }

  state_handle->device_info.name = (char *)malloc(sizeof(char) * 8);
  if (state_handle->device_info.name == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for device name");
    return ESP_ERR_NO_MEM;
  }
  snprintf(state_handle->device_info.name, 8, "UNKNOWN");

  *state_handle_ptr = state_handle;

  return ESP_OK;
}
