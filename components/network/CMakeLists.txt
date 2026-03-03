idf_component_register(
  SRCS "network.c" "peers.c" "udp.c" "wifi.c" "messages.c" "events.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES "esp_wifi" "esp_event" "lwip" "esp_timer"
  REQUIRED_IDF_TARGETS esp32
)