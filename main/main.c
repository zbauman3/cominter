#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"

#include "application/state.h"
#include "network/udp.h"
#include "network/wifi.h"
#include "storage/nvs.h"

static char *TAG = "MULTICAST";

esp_err_t init_app() {
  esp_err_t ret = ESP_OK;

  ret = storage_nvs_init();
  if (ret != ESP_OK) {
    return ret;
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK) {
    return ret;
  }

  device_state_handle_t device_state_handle;
  ret = device_state_init(&device_state_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = storage_nvs_get_name(device_state_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = udp_multicast_init(device_state_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = wifi_init(device_state_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  return ret;
}

void app_main(void) {
  esp_err_t err = init_app();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) initializing app. Restarting...",
             esp_err_to_name(err));

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    esp_restart();
  }

  ESP_LOGD(TAG, "App initialized successfully");
}
