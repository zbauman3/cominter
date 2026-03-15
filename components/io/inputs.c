#include "driver/gpio.h"
#include "esp_log.h"

#include "io/inputs.h"
#include "protocols/messages.h"

static const char *BASE_TAG = "IO:INPUTS";
static const char *TASK_TAG = "IO:INPUTS:TASK";

void IRAM_ATTR io_inputs_talk_btn_isr(void *arg) {
  io_inputs_handle_t io_inputs_handle = (io_inputs_handle_t)arg;
  xQueueSendFromISR(io_inputs_handle->queues.inputs_queue,
                    &io_inputs_handle->pins.talk_btn, NULL);
}

void io_inputs_task(void *pvParameters) {
  io_inputs_handle_t io_inputs_handle = (io_inputs_handle_t)pvParameters;
  uint32_t io_num;
  app_message_handle_t outgoing_message;

  while (1) {
    xQueueReceive(io_inputs_handle->queues.inputs_queue, &io_num,
                  portMAX_DELAY);
    if (io_num != io_inputs_handle->pins.talk_btn) {
      continue;
    }

    ESP_LOGI(TASK_TAG, "Talk button pressed");

    if (app_message_init_text(&outgoing_message, "Button pressed!",
                              io_inputs_handle->device_info->mac_address,
                              NULL) != ESP_OK) {
      ESP_LOGE(TASK_TAG, "Failed to initialize message");
      continue;
    }

    if (app_queues_add_outgoing_message(io_inputs_handle->app_queues,
                                        &outgoing_message, pdMS_TO_TICKS(500),
                                        false) != ESP_OK) {
      ESP_LOGE(TASK_TAG, "Failed to send button message to queue");
      app_message_free(outgoing_message);
      outgoing_message = NULL;
    }
  }
}

esp_err_t io_inputs_init(io_inputs_handle_t *io_inputs_handle_ptr,
                         int talk_btn_pin,
                         app_device_info_handle_t device_info_handle,
                         app_queues_handle_t app_queues_handle) {
  io_inputs_handle_t io_inputs_handle =
      (io_inputs_handle_t)malloc(sizeof(io_inputs_t));
  if (io_inputs_handle == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to allocate memory for io inputs handle");
    return ESP_ERR_NO_MEM;
  }

  io_inputs_handle->device_info = device_info_handle;
  io_inputs_handle->app_queues = app_queues_handle;

  io_inputs_handle->queues.inputs_queue = xQueueCreate(10, sizeof(uint32_t));
  if (io_inputs_handle->queues.inputs_queue == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create inputs queue");
    return ESP_ERR_NO_MEM;
  }

  io_inputs_handle->pins.talk_btn = talk_btn_pin;

  // zero-initialize the config structure.
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << io_inputs_handle->pins.talk_btn);
  io_conf.pull_down_en = 0;
  // there's already a pull-up resistor on the pin
  io_conf.pull_up_en = 0;

  gpio_config(&io_conf);

  // setup per-pin ISRs.
  // we only want to detect edges.
  gpio_install_isr_service(ESP_INTR_FLAG_EDGE);

  // register the ISR handler for the talk button
  gpio_isr_handler_add(io_inputs_handle->pins.talk_btn, io_inputs_talk_btn_isr,
                       io_inputs_handle);

  BaseType_t xReturned =
      xTaskCreate(io_inputs_task, TASK_TAG, IO_INPUTS_TASK_STACK_DEPTH_INPUTS,
                  io_inputs_handle, IO_INPUTS_TASK_PRIORITY_INPUTS,
                  &io_inputs_handle->tasks.inputs_task);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create inputs task");
    return ESP_ERR_INVALID_STATE;
  }
  if (io_inputs_handle->tasks.inputs_task == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create inputs task");
    return ESP_ERR_NO_MEM;
  }

  *io_inputs_handle_ptr = io_inputs_handle;

  return ESP_OK;
}