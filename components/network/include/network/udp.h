#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "application/state.h"
#include "network/events.h"
#include "network/peers.h"
#include "network/queues.h"
#include "network/types.h"

#define NETWORK_UDP_HEARTBEAT_INTERVAL_MS 10000
#define NETWORK_UDP_HEARTBEAT_INIT_INTERVAL_MS 1000

#define NETWORK_UDP_TASK_PRIORITY_SOCKET 6
#define NETWORK_UDP_TASK_PRIORITY_MULTICAST 5
#define NETWORK_UDP_TASK_PRIORITY_UDP_HEARTBEAT 3

#define NETWORK_UDP_TASK_STACK_DEPTH_SOCKET                                    \
  ((1024 * 3) + NETWORK_MESSAGE_MAX_LENGTH)
#define NETWORK_UDP_TASK_STACK_DEPTH_MULTICAST                                 \
  ((1024 * 3) + NETWORK_MESSAGE_MAX_LENGTH)

#define APP_STATE_TASK_STACK_DEPTH_UDP_HEARTBEAT 1024

typedef struct network_udp_t {
  int socket;
  struct addrinfo *multicast_addr_info;
  esp_netif_ip_info_t *ip_info;
  // this is the MAC address of the device
  network_mac_address_t mac_address;

  struct {
    TaskHandle_t socket;
    TaskHandle_t multicast_read;
    TaskHandle_t multicast_write;
    TaskHandle_t udp_heartbeat;
  } tasks;

  network_events_handle_t events;
  network_queues_handle_t queues;
  network_peers_list_handle_t peers;
  app_state_handle_t state;
} network_udp_t;

typedef network_udp_t *network_udp_handle_t;

esp_err_t network_udp_init(network_udp_handle_t *network_udp_handle_ptr,
                           network_events_handle_t events_handle,
                           network_queues_handle_t queues_handle,
                           network_peers_list_handle_t peers_handle,
                           app_state_handle_t state_handle);
void network_udp_socket_close(network_udp_handle_t network_udp_handle);