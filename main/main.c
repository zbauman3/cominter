#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"

#include "application/device_info.h"
#include "application/message_handler.h"
#include "application/peers.h"
#include "application/queues.h"
#include "io/inputs.h"
#include "network/events.h"
#include "network/udp.h"
#include "network/wifi.h"
#include "storage/nvs.h"

static char *TAG = "APP_MAIN";

#define TALK_BTN_PIN GPIO_NUM_35

static app_device_info_handle_t device_info_handle;
static io_inputs_handle_t io_inputs_handle;
static network_events_handle_t network_events_handle;
static app_peers_handle_t app_peers_handle;
static app_queues_handle_t app_queues_handle;
static network_udp_handle_t network_udp_handle;
static network_wifi_handle_t network_wifi_handle;
static app_message_handler_handle_t app_message_handler_handle;

esp_err_t init_app() {
  esp_err_t ret = ESP_OK;

  ESP_GOTO_ON_ERROR(storage_nvs_init(), init_app_cleanup, TAG,
                    "Failed to initialize NVS");

  ESP_GOTO_ON_ERROR(esp_event_loop_create_default(), init_app_cleanup, TAG,
                    "Failed to create event loop");

  ESP_GOTO_ON_ERROR(app_device_info_init(&device_info_handle), init_app_cleanup,
                    TAG, "Failed to initialize app device info");

  ESP_GOTO_ON_ERROR(network_events_init(&network_events_handle),
                    init_app_cleanup, TAG,
                    "Failed to initialize network events");

  ESP_GOTO_ON_ERROR(app_queues_init(&app_queues_handle), init_app_cleanup, TAG,
                    "Failed to initialize network queues");

  ESP_GOTO_ON_ERROR(network_udp_init(&network_udp_handle, network_events_handle,
                                     app_queues_handle, device_info_handle),
                    init_app_cleanup, TAG, "Failed to initialize network UDP");

  ESP_GOTO_ON_ERROR(network_wifi_init(&network_wifi_handle,
                                      network_events_handle,
                                      network_udp_handle),
                    init_app_cleanup, TAG, "Failed to initialize network WiFi");

  ESP_GOTO_ON_ERROR(
      app_peers_init(&app_peers_handle, device_info_handle, app_queues_handle),
      init_app_cleanup, TAG, "Failed to initialize network peers");

  ESP_GOTO_ON_ERROR(
      app_message_handler_init(&app_message_handler_handle, app_peers_handle,
                               app_queues_handle, device_info_handle),
      init_app_cleanup, TAG, "Failed to initialize app message handler");

  ESP_GOTO_ON_ERROR(io_inputs_init(&io_inputs_handle, TALK_BTN_PIN,
                                   device_info_handle, app_queues_handle),
                    init_app_cleanup, TAG, "Failed to initialize IO inputs");

  ESP_LOGI(TAG, "Device name: %s", device_info_handle->name);
  ESP_LOGI(
      TAG, "Device MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
      device_info_handle->mac_address[0], device_info_handle->mac_address[1],
      device_info_handle->mac_address[2], device_info_handle->mac_address[3],
      device_info_handle->mac_address[4], device_info_handle->mac_address[5]);

init_app_cleanup:
  // no cleanup needed. We're just going to restart the app.
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
