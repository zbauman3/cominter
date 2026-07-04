idf_component_register(
    SRCS "test_mic.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_adc esp_driver_dac
)
