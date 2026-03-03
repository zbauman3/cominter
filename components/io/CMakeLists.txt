idf_component_register(
  SRCS "inputs.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES "driver" "network"
  REQUIRED_IDF_TARGETS esp32
)