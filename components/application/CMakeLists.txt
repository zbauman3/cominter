idf_component_register(
  SRCS "device_info.c" "message_handler.c" "peers.c" "queues.c"
  INCLUDE_DIRS "include"
  REQUIRES "protocols"
  PRIV_REQUIRES "esp_timer" "storage"
  REQUIRED_IDF_TARGETS esp32
)