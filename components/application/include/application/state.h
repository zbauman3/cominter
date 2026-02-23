#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <lwip/netdb.h>

#define STATE_TASK_PRIORITY_SOCKET 6
#define STATE_TASK_PRIORITY_MULTICAST 5
#define STATE_TASK_PRIORITY_INPUTS 4

#define STATE_TASK_STACK_DEPTH_SOCKET 1024 * 4
#define STATE_TASK_STACK_DEPTH_MULTICAST 1024 * 4
#define STATE_TASK_STACK_DEPTH_INPUTS 1024 * 2

#define STATE_NETWORK_EVENT_GOT_NEW_IP (1 << 0)
#define STATE_NETWORK_EVENT_SOCKET_READY (1 << 1)

typedef struct state_t {
  esp_netif_ip_info_t *ip_info;
  char *device_name;
  EventGroupHandle_t network_events;
  TaskHandle_t task_socket;
  TaskHandle_t task_multicast_read;
  TaskHandle_t task_multicast_write;
  TaskHandle_t task_inputs;
  int socket;
  struct addrinfo *multicast_addr_info;
  QueueHandle_t inputs_queue;
  struct {
    int talk_btn;
  } pins;
} state_t;
typedef state_t *state_handle_t;

esp_err_t state_init(state_handle_t *handle, int talk_btn_pin);