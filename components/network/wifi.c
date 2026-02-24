#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

#include "network/udp.h"
#include "network/wifi.h"

static const char *TAG = "NETWORK:WIFI";

// handles wifi events and updates the state accordingly
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {

  if (event_base == IP_EVENT) {
    app_state_handle_t state_handle = (app_state_handle_t)arg;

    switch (event_id) {
    case IP_EVENT_STA_GOT_IP: {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGD(TAG, "EVENT - IP_EVENT_STA_GOT_IP");
      ESP_LOGD(TAG, "IPV4 is: " IPSTR, IP2STR(&event->ip_info.ip));
      memcpy(state_handle->network.ip_info, &event->ip_info,
             sizeof(esp_netif_ip_info_t));
      // signal that we've got a new IP so that the socket can be created
      xEventGroupSetBits(state_handle->event_groups.network_events,
                         APP_STATE_NETWORK_EVENT_GOT_NEW_IP);
      break;
    }
    case IP_EVENT_STA_LOST_IP: {
      ESP_LOGD(TAG, "EVENT - IP_EVENT_STA_LOST_IP");
      // Close the socket, it will be recreated when we get a new IP
      network_udp_socket_close(state_handle);
      state_handle->network.ip_info->ip = (esp_ip4_addr_t){0};
      state_handle->network.ip_info->netmask = (esp_ip4_addr_t){0};
      state_handle->network.ip_info->gw = (esp_ip4_addr_t){0};
      break;
    }
    default: {
      ESP_LOGD(TAG, "IP_EVENT - %ld", event_id);
      break;
    }
    }
    return;
  }

  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START: {
      ESP_LOGD(TAG, "EVENT - WIFI_EVENT_STA_START");
      esp_wifi_connect();
      break;
    }
    case WIFI_EVENT_STA_CONNECTED: {
      ESP_LOGD(TAG, "EVENT - WIFI_EVENT_STA_CONNECTED");
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      ESP_LOGD(TAG, "EVENT - WIFI_EVENT_STA_DISCONNECTED");
      esp_wifi_connect();

      break;
    }
    default: {
      ESP_LOGD(TAG, "WIFI_EVENT - %ld", event_id);
      break;
    }
    }

    return;
  }
}

// Using overview from:
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/wifi.html#esp32-wi-fi-station-general-scenario
esp_err_t network_wifi_init(app_state_handle_t state_handle) {
  ESP_LOGD(TAG, "Starting WiFi connection to \"%s\"", CONFIG_WIFI_SSID);

  esp_err_t err = ESP_OK;

  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler,
                                   state_handle);
  if (err != ESP_OK) {
    return err;
  }

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler,
                                   state_handle);
  if (err != ESP_OK) {
    return err;
  }

  err = esp_netif_init();
  if (err != ESP_OK) {
    return err;
  }

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    return err;
  }

  // get the current mode from NVS. If not correct, set it
  wifi_mode_t wifi_nvs_mode;
  esp_err_t get_mode_ret = esp_wifi_get_mode(&wifi_nvs_mode);
  if (get_mode_ret != ESP_OK || wifi_nvs_mode != WIFI_MODE_STA) {
    ESP_LOGW(TAG, "WiFi NVS \"mode\" not correct - setting");
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
      return err;
    }
  }

  // get the current config from NVS. If not correct, set it
  wifi_config_t wifi_nvs_config;
  esp_err_t get_config_ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_nvs_config);
  if (get_config_ret != ESP_OK ||
      strcmp((char *)wifi_nvs_config.sta.ssid, CONFIG_WIFI_SSID) != 0 ||
      strcmp((char *)wifi_nvs_config.sta.password, CONFIG_WIFI_PWD) != 0) {
    ESP_LOGW(TAG, "WiFi NVS \"config\" not correct - setting");
    wifi_config_t wifi_config_new = {
        .sta =
            {
                .ssid = CONFIG_WIFI_SSID,
                .password = CONFIG_WIFI_PWD,
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                .threshold =
                    {
                        .rssi = 0,
                        .authmode = 0,
                    },
            },
    };

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config_new);
    if (err != ESP_OK) {
      return err;
    }
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    return err;
  }

  return err;
}