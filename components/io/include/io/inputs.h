#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <lwip/netdb.h>

#define IO_INPUTS_TASK_PRIORITY_INPUTS 4

#define IO_INPUTS_TASK_STACK_DEPTH_INPUTS 1024 * 2

typedef struct io_inputs_t {
  struct {
    TaskHandle_t inputs_task;
  } tasks;
  struct {
    QueueHandle_t inputs_queue;
  } queues;
  struct {
    int talk_btn;
  } pins;
} io_inputs_t;

typedef io_inputs_t *io_inputs_handle_t;

esp_err_t io_inputs_init(io_inputs_handle_t *io_inputs_handle_ptr,
                         int talk_btn_pin);
void io_inputs_free(io_inputs_handle_t io_inputs_handle);