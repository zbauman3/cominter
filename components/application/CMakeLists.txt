idf_component_register(
  SRCS "state.c" "messages.c"
  INCLUDE_DIRS "include"
  REQUIRES "esp_netif"
  PRIV_REQUIRES "esp_timer"
  REQUIRED_IDF_TARGETS esp32
)