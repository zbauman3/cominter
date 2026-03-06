#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"

#include "application/state.h"
#include "io/inputs.h"
#include "network/events.h"
#include "network/peers.h"
#include "network/queues.h"
#include "network/udp.h"
#include "network/wifi.h"
#include "storage/nvs.h"

static char *TAG = "APP_MAIN";

#define TALK_BTN_PIN GPIO_NUM_35

static app_state_handle_t state_handle;
static io_inputs_handle_t io_inputs_handle;
static network_events_handle_t network_events_handle;
static network_peers_list_handle_t network_peers_list_handle;
static network_queues_handle_t network_queues_handle;
static network_udp_handle_t network_udp_handle;
static network_wifi_handle_t network_wifi_handle;

esp_err_t init_app() {
  esp_err_t ret = ESP_OK;

  ESP_GOTO_ON_ERROR(storage_nvs_init(), init_app_cleanup, TAG,
                    "Failed to initialize NVS");

  ESP_GOTO_ON_ERROR(esp_event_loop_create_default(), init_app_cleanup, TAG,
                    "Failed to create event loop");

  ESP_GOTO_ON_ERROR(app_state_init(&state_handle), init_app_cleanup, TAG,
                    "Failed to initialize app state");

  ESP_GOTO_ON_ERROR(storage_nvs_get_name(state_handle), init_app_cleanup, TAG,
                    "Failed to get device name from NVS");

  ESP_GOTO_ON_ERROR(network_events_init(&network_events_handle),
                    init_app_cleanup, TAG,
                    "Failed to initialize network events");

  ESP_GOTO_ON_ERROR(network_peers_init(&network_peers_list_handle),
                    init_app_cleanup, TAG,
                    "Failed to initialize network peers");

  ESP_GOTO_ON_ERROR(network_queues_init(&network_queues_handle),
                    init_app_cleanup, TAG,
                    "Failed to initialize network queues");

  ESP_GOTO_ON_ERROR(network_udp_init(&network_udp_handle, network_events_handle,
                                     network_queues_handle,
                                     network_peers_list_handle, state_handle),
                    init_app_cleanup, TAG, "Failed to initialize network UDP");

  ESP_GOTO_ON_ERROR(network_wifi_init(&network_wifi_handle,
                                      network_events_handle,
                                      network_udp_handle),
                    init_app_cleanup, TAG, "Failed to initialize network WiFi");

  ESP_GOTO_ON_ERROR(io_inputs_init(&io_inputs_handle, TALK_BTN_PIN,
                                   state_handle, network_queues_handle),
                    init_app_cleanup, TAG, "Failed to initialize IO inputs");

  ESP_LOGI(TAG, "Device name: %s", state_handle->device_info.name);
  ESP_LOGI(TAG, "MAC address string: %s", state_handle->device_info.name);

init_app_cleanup:
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
