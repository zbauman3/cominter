#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"

#include "application/state.h"
#include "io/inputs.h"
#include "network/udp.h"
#include "network/wifi.h"
#include "storage/nvs.h"

static char *TAG = "MULTICAST";

#define TALK_BTN_PIN GPIO_NUM_35

static state_handle_t state_handle;

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

  ret = state_init(&state_handle, TALK_BTN_PIN);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = storage_nvs_get_name(state_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = udp_multicast_init(state_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = io_inputs_init(state_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = wifi_init(state_handle);
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
