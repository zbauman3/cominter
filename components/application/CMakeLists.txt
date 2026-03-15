idf_component_register(
  SRCS "device_info.c" "message_handler.c" "messages.c" "peers.c" "queues.c"
  INCLUDE_DIRS "include"
  REQUIRES "network"
  PRIV_REQUIRES "esp_timer" "storage"
  REQUIRED_IDF_TARGETS esp32
)