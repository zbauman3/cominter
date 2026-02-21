#include "driver/gpio.h"
#include "esp_log.h"

#include "io/inputs.h"

static const char *BASE_TAG = "IO:INPUTS";
static const char *TASK_TAG = "IO:INPUTS:TASK";

void IRAM_ATTR io_inputs_talk_btn_isr(void *arg) {
  device_state_handle_t device_state_handle = (device_state_handle_t)arg;
  xQueueSendFromISR(device_state_handle->inputs_queue,
                    &device_state_handle->pins.talk_btn, NULL);
}

void io_inputs_task(void *pvParameters) {
  device_state_handle_t device_state_handle =
      (device_state_handle_t)pvParameters;

  uint32_t io_num;
  while (1) {
    xQueueReceive(device_state_handle->inputs_queue, &io_num, portMAX_DELAY);
    if (io_num == device_state_handle->pins.talk_btn) {
      ESP_LOGI(TASK_TAG, "Talk button pressed");
    }
  }
}

esp_err_t io_inputs_init(device_state_handle_t device_state_handle) {
  // zero-initialize the config structure.
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << device_state_handle->pins.talk_btn);
  io_conf.pull_down_en = 0;
  // there's already a pull-up resistor on the pin
  io_conf.pull_up_en = 0;

  gpio_config(&io_conf);

  // setup per-pin ISRs.
  // we only want to detect edges.
  gpio_install_isr_service(ESP_INTR_FLAG_EDGE);

  // register the ISR handler for the talk button
  gpio_isr_handler_add(device_state_handle->pins.talk_btn,
                       io_inputs_talk_btn_isr, device_state_handle);

  BaseType_t xReturned =
      xTaskCreate(io_inputs_task, TASK_TAG, STATE_TASK_STACK_DEPTH_INPUTS,
                  device_state_handle, STATE_TASK_PRIORITY_INPUTS,
                  &device_state_handle->task_inputs);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create inputs task");
    return ESP_ERR_INVALID_STATE;
  }

  if (device_state_handle->task_inputs == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create inputs task");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}