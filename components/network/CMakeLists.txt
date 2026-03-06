idf_component_register(
  SRCS "peers.c" "udp.c" "wifi.c" "messages.c" "events.c" "queues.c"
  INCLUDE_DIRS "include"
  REQUIRES "application"
  PRIV_REQUIRES "esp_wifi" "esp_event" "lwip" "esp_timer"
  REQUIRED_IDF_TARGETS esp32
)