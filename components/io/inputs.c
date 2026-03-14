#include "driver/gpio.h"
#include "esp_log.h"

#include "io/inputs.h"
#include "network/messages.h"

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
  network_message_handle_t outgoing_message;
  BaseType_t xReturned;

  while (1) {
    xQueueReceive(io_inputs_handle->queues.inputs_queue, &io_num,
                  portMAX_DELAY);
    if (io_num != io_inputs_handle->pins.talk_btn) {
      continue;
    }

    ESP_LOGI(TASK_TAG, "Talk button pressed");

    if (network_message_init_text(&outgoing_message, "Hi!", NULL, NULL) !=
        ESP_OK) {
      ESP_LOGE(TASK_TAG, "Failed to initialize message");
      continue;
    }

    xReturned =
        xQueueSendToBack(io_inputs_handle->network_queues->message_outgoing,
                         &outgoing_message, (500 / portTICK_PERIOD_MS));
    if (xReturned != pdPASS) {
      ESP_LOGE(TASK_TAG, "Failed to send message to queue. Dropping message.");
      network_message_free(outgoing_message);
    }

    // the queue will now own the message.
    outgoing_message = NULL;
  }
}

esp_err_t io_inputs_init(io_inputs_handle_t *io_inputs_handle_ptr,
                         int talk_btn_pin, app_state_handle_t state_handle,
                         network_queues_handle_t network_queues_handle) {
  io_inputs_handle_t io_inputs_handle =
      (io_inputs_handle_t)malloc(sizeof(io_inputs_t));
  if (io_inputs_handle == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to allocate memory for io inputs handle");
    return ESP_ERR_NO_MEM;
  }

  io_inputs_handle->state = state_handle;
  io_inputs_handle->network_queues = network_queues_handle;

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