idf_component_register(
  SRCS "state.c" "persistence.c"
  INCLUDE_DIRS "include"
  REQUIRES "esp_netif" "nvs_flash"
  REQUIRED_IDF_TARGETS esp32
)