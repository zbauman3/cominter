#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "application/device_info.h"
#include "application/queues.h"
#include "network/events.h"

#define NETWORK_UDP_TASK_PRIORITY_SOCKET 6
#define NETWORK_UDP_TASK_PRIORITY_MULTICAST 5

#define NETWORK_UDP_TASK_STACK_DEPTH_SOCKET                                    \
  ((1024 * 5) + APP_MESSAGE_MAX_LENGTH)
#define NETWORK_UDP_TASK_STACK_DEPTH_MULTICAST                                 \
  ((1024 * 7) + APP_MESSAGE_MAX_LENGTH)

typedef struct network_udp_t {
  int socket;
  struct addrinfo *multicast_addr_info;
  esp_netif_ip_info_t *ip_info;

  struct {
    TaskHandle_t socket;
    TaskHandle_t multicast_read;
    TaskHandle_t multicast_write;
  } tasks;

  network_events_handle_t events;
  app_queues_handle_t queues;
  app_device_info_handle_t device_info;
} network_udp_t;

typedef network_udp_t *network_udp_handle_t;

esp_err_t network_udp_init(network_udp_handle_t *network_udp_handle_ptr,
                           network_events_handle_t events_handle,
                           app_queues_handle_t queues_handle,
                           app_device_info_handle_t device_info_handle);