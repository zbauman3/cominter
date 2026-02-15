idf_component_register(
  SRCS "wifi.c"
  INCLUDE_DIRS "include"
  REQUIRES "esp_netif"
  PRIV_REQUIRES "esp_wifi" "esp_event"
  REQUIRED_IDF_TARGETS esp32
)