#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "storage/nvs.h"

static const char *TAG = "STORAGE:NVS";

esp_err_t storage_nvs_init() {
  esp_err_t err = ESP_OK;

  err = nvs_flash_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) initializing NVS flash!", esp_err_to_name(err));
    return err;
  }

  return ESP_OK;
}

esp_err_t storage_nvs_get_name(char **name_ptr) {
  esp_err_t ret = ESP_OK;
  nvs_handle_t nvs_handle = 0;

  ESP_GOTO_ON_ERROR(nvs_open_from_partition("nvs", NVS_DEVICE_INFO_NAMESPACE,
                                            NVS_READONLY, &nvs_handle),
                    storage_nvs_get_name_cleanup, TAG,
                    "Error (%s) opening NVS handle!", esp_err_to_name(ret));

  size_t str_len = 0;
  // this length includes the null terminator
  ESP_GOTO_ON_ERROR(
      nvs_get_str(nvs_handle, NVS_DEVICE_INFO_NAME_KEY, NULL, &str_len),
      storage_nvs_get_name_cleanup, TAG, "Error (%s) getting string length!",
      esp_err_to_name(ret));

  if (*name_ptr != NULL) {
    free(*name_ptr);
  }
  *name_ptr = (char *)malloc(str_len);
  if (*name_ptr == NULL) {
    ret = ESP_ERR_NO_MEM;
    goto storage_nvs_get_name_cleanup;
  }

  ESP_GOTO_ON_ERROR(
      nvs_get_str(nvs_handle, NVS_DEVICE_INFO_NAME_KEY, *name_ptr, &str_len),
      storage_nvs_get_name_cleanup, TAG, "Error (%s) getting string value!",
      esp_err_to_name(ret));

storage_nvs_get_name_cleanup:
  if (nvs_handle != 0) {
    nvs_close(nvs_handle);
  }

  return ret;
}