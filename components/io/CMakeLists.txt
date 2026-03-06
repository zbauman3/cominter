idf_component_register(
  SRCS "inputs.c"
  INCLUDE_DIRS "include"
  REQUIRES "application" "network"
  PRIV_REQUIRES "driver"
  REQUIRED_IDF_TARGETS esp32
)