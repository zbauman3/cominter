idf_component_register(
  SRCS "messages.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES "esp_timer"
  REQUIRED_IDF_TARGETS esp32
)