#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

#include "network/wifi.h"

static const char *TAG = "NETWORK:WIFI";

// handles wifi events and updates the state accordingly
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {

  if (event_base == IP_EVENT) {
    network_wifi_handle_t wifi_handle = (network_wifi_handle_t)arg;

    switch (event_id) {
    case IP_EVENT_STA_GOT_IP: {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGD(TAG, "EVENT - IP_EVENT_STA_GOT_IP");
      ESP_LOGD(TAG, "IPV4 is: " IPSTR, IP2STR(&event->ip_info.ip));
      memcpy(wifi_handle->udp->ip_info, &event->ip_info,
             sizeof(esp_netif_ip_info_t));
      // signal that we've got a new IP so that the socket can be created
      xEventGroupSetBits(wifi_handle->events->group_handle,
                         NETWORK_EVENT_GOT_NEW_IP);
      break;
    }
    case IP_EVENT_STA_LOST_IP: {
      ESP_LOGD(TAG, "EVENT - IP_EVENT_STA_LOST_IP");
      // signal that we've lost our IP so that the socket can be closed
      xEventGroupSetBits(wifi_handle->events->group_handle,
                         NETWORK_EVENT_LOST_IP);
      wifi_handle->udp->ip_info->ip = (esp_ip4_addr_t){0};
      wifi_handle->udp->ip_info->netmask = (esp_ip4_addr_t){0};
      wifi_handle->udp->ip_info->gw = (esp_ip4_addr_t){0};
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
esp_err_t network_wifi_init(network_wifi_handle_t *wifi_handle_ptr,
                            network_events_handle_t events,
                            network_udp_handle_t udp) {
  esp_err_t ret = ESP_OK;

  network_wifi_handle_t wifi_handle =
      (network_wifi_handle_t)malloc(sizeof(network_wifi_t));
  ESP_GOTO_ON_FALSE(wifi_handle != NULL, ESP_ERR_NO_MEM,
                    network_wifi_init_error, TAG,
                    "Failed to allocate memory for wifi handle");

  wifi_handle->events = events;
  wifi_handle->udp = udp;

  ESP_GOTO_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, wifi_handle),
                    network_wifi_init_error, TAG,
                    "Failed to register event handler");

  ESP_GOTO_ON_ERROR(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, wifi_handle),
                    network_wifi_init_error, TAG,
                    "Failed to register event handler");

  ESP_GOTO_ON_ERROR(esp_netif_init(), network_wifi_init_error, TAG,
                    "Failed to initialize network interface");

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_GOTO_ON_ERROR(esp_wifi_init(&cfg), network_wifi_init_error, TAG,
                    "Failed to initialize WiFi");

  // get the current mode from NVS. If not correct, set it
  wifi_mode_t wifi_nvs_mode;
  ret = esp_wifi_get_mode(&wifi_nvs_mode);
  if (ret != ESP_OK || wifi_nvs_mode != WIFI_MODE_STA) {
    ret = ESP_OK; // reset to try more below
    ESP_LOGW(TAG, "WiFi NVS \"mode\" not correct - setting");
    ESP_GOTO_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), network_wifi_init_error,
                      TAG, "Failed to set WiFi mode");
  }

  // get the current config from NVS. If not correct, set it
  wifi_config_t wifi_nvs_config;
  ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_nvs_config);
  if (ret != ESP_OK ||
      strcmp((char *)wifi_nvs_config.sta.ssid, CONFIG_WIFI_SSID) != 0 ||
      strcmp((char *)wifi_nvs_config.sta.password, CONFIG_WIFI_PWD) != 0) {
    ret = ESP_OK; // reset to try more below
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

    ESP_GOTO_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_new),
                      network_wifi_init_error, TAG,
                      "Failed to set WiFi config");
  }

  ESP_GOTO_ON_ERROR(esp_wifi_start(), network_wifi_init_error, TAG,
                    "Failed to start WiFi");

  *wifi_handle_ptr = wifi_handle;
  return ESP_OK;

network_wifi_init_error:
  return ret;
}