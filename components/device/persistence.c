#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

#include "device/persistence.h"

static const char *TAG = "DEVICE:NVS";

esp_err_t persistence_init() {
  esp_err_t err = ESP_OK;

  err = nvs_flash_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) initializing NVS flash!", esp_err_to_name(err));
    return err;
  }

  return ESP_OK;
}

esp_err_t persistence_fetch_name(device_state_handle_t device_state_handle) {
  esp_err_t ret = ESP_OK;
  nvs_handle_t nvs_handle;

  ESP_GOTO_ON_ERROR(nvs_open_from_partition("nvs", DEVICE_INFO_NAMESPACE,
                                            NVS_READONLY, &nvs_handle),
                    fetch_name_cleanup, TAG, "Error (%s) opening NVS handle!",
                    esp_err_to_name(ret));

  size_t str_len = 0;
  // this length includes the null terminator
  ESP_GOTO_ON_ERROR(
      nvs_get_str(nvs_handle, DEVICE_INFO_NAME_KEY, NULL, &str_len),
      fetch_name_cleanup, TAG, "Error (%s) getting string length!",
      esp_err_to_name(ret));

  if (device_state_handle->device_name != NULL) {
    free(device_state_handle->device_name);
  }
  device_state_handle->device_name = (char *)malloc(str_len);

  ESP_GOTO_ON_ERROR(nvs_get_str(nvs_handle, DEVICE_INFO_NAME_KEY,
                                device_state_handle->device_name, &str_len),
                    fetch_name_cleanup, TAG, "Error (%s) getting string value!",
                    esp_err_to_name(ret));

fetch_name_cleanup:
  nvs_close(nvs_handle);

  return ret;
}