#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>

#include "application/device_info.h"
#include "storage/nvs.h"

static const char *TAG = "APPLICATION:DEVICE_INFO";

esp_err_t
app_device_info_init(app_device_info_handle_t *device_info_handle_ptr) {
  app_device_info_handle_t device_info_handle =
      (app_device_info_handle_t)malloc(sizeof(app_device_info_t));
  if (device_info_handle == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for device info handle");
    return ESP_ERR_NO_MEM;
  }

  device_info_handle->name = NULL;
  memset(device_info_handle->mac_address, 0, sizeof(network_mac_address_t));

  esp_err_t ret = storage_nvs_get_name(&device_info_handle->name);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get device name from NVS");
    return ret;
  }

  ret = esp_read_mac(device_info_handle->mac_address, ESP_MAC_WIFI_STA);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read MAC address");
    return ret;
  };

  *device_info_handle_ptr = device_info_handle;

  return ESP_OK;
}
