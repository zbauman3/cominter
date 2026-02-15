idf_component_register(
  SRCS "udp.c" "wifi.c"
  INCLUDE_DIRS "include"
  REQUIRES "application"
  PRIV_REQUIRES "esp_wifi" "esp_event" "lwip"
  REQUIRED_IDF_TARGETS esp32
)