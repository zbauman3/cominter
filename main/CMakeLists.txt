idf_component_register(
  SRCS "main.c"
  REQUIRED_IDF_TARGETS esp32
  REQUIRES test_mic application io network storage esp_event
)
