#pragma once

#include "esp_err.h"

// Call this from app_main() instead of init_app() to run the mic test.
// It sets up the button and ADC, then waits for a button press to
// capture and dump audio samples over serial.
esp_err_t test_mic_run(void);
