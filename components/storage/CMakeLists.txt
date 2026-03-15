idf_component_register(
  SRCS "nvs.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES "nvs_flash"
  REQUIRED_IDF_TARGETS esp32
)