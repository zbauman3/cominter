#include "driver/gpio.h"
#include "esp_log.h"

#include "application/messages.h"
#include "io/inputs.h"

static const char *BASE_TAG = "IO:INPUTS";
static const char *TASK_TAG = "IO:INPUTS:TASK";

void IRAM_ATTR io_inputs_talk_btn_isr(void *arg) {
  app_state_handle_t state_handle = (app_state_handle_t)arg;
  xQueueSendFromISR(state_handle->queues.inputs_queue,
                    &state_handle->pins.talk_btn, NULL);
}

void io_inputs_task(void *pvParameters) {
  app_state_handle_t state_handle = (app_state_handle_t)pvParameters;
  uint32_t io_num;
  app_message_handle_t outgoing_message;
  BaseType_t xReturned;

  while (1) {
    xQueueReceive(state_handle->queues.inputs_queue, &io_num, portMAX_DELAY);
    if (io_num != state_handle->pins.talk_btn) {
      continue;
    }

    if (app_message_init_text(state_handle, &outgoing_message, "Hi!", NULL) !=
        ESP_OK) {
      ESP_LOGE(TASK_TAG, "Failed to initialize message");
      continue;
    }

    xReturned = xQueueSendToBack(state_handle->queues.message_outgoing_queue,
                                 &outgoing_message, (500 / portTICK_PERIOD_MS));
    if (xReturned != pdPASS) {
      ESP_LOGE(TASK_TAG, "Failed to send message to queue. Dropping message.");
      app_message_free(outgoing_message);
    }

    // the queue will now own the message.
    outgoing_message = NULL;
  }
}

esp_err_t io_inputs_init(app_state_handle_t state_handle) {
  // zero-initialize the config structure.
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << state_handle->pins.talk_btn);
  io_conf.pull_down_en = 0;
  // there's already a pull-up resistor on the pin
  io_conf.pull_up_en = 0;

  gpio_config(&io_conf);

  // setup per-pin ISRs.
  // we only want to detect edges.
  gpio_install_isr_service(ESP_INTR_FLAG_EDGE);

  // register the ISR handler for the talk button
  gpio_isr_handler_add(state_handle->pins.talk_btn, io_inputs_talk_btn_isr,
                       state_handle);

  BaseType_t xReturned = xTaskCreate(
      io_inputs_task, TASK_TAG, APP_STATE_TASK_STACK_DEPTH_INPUTS, state_handle,
      APP_STATE_TASK_PRIORITY_INPUTS, &state_handle->tasks.inputs_task);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create inputs task");
    return ESP_ERR_INVALID_STATE;
  }

  if (state_handle->tasks.inputs_task == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create inputs task");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}