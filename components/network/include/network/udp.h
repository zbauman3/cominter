#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "network/events.h"

// #define NETWORK_UDP_HEARTBEAT_INTERVAL_MS 10000

#define NETWORK_UDP_TASK_PRIORITY_SOCKET 6
#define NETWORK_UDP_TASK_PRIORITY_MULTICAST 5
// #define NETWORK_UDP_TASK_PRIORITY_UDP_HEARTBEAT 3

#define NETWORK_UDP_TASK_STACK_DEPTH_SOCKET                                    \
  ((1024 * 3) + NETWORK_MESSAGE_MAX_LENGTH)
#define NETWORK_UDP_TASK_STACK_DEPTH_MULTICAST                                 \
  ((1024 * 3) + NETWORK_MESSAGE_MAX_LENGTH)

// #define APP_STATE_TASK_STACK_DEPTH_UDP_HEARTBEAT 1024

typedef uint8_t network_udp_mac_address_t[6];

typedef struct network_udp_t {
  int socket;
  struct addrinfo *multicast_addr_info;
  esp_netif_ip_info_t *ip_info;
  // this is the MAC address of the device
  network_udp_mac_address_t mac_address;

  struct {
    TaskHandle_t socket;
    TaskHandle_t multicast_read;
    TaskHandle_t multicast_write;
    // TaskHandle_t udp_heartbeat;
  } tasks;

  network_events_handle_t events;

  struct {
    // This contains a pointer to a message.
    // Readers must free the message after use.
    // Writers must not interact with the message.
    QueueHandle_t message_outgoing;
    // QueueHandle_t message_incoming;
  } queues;
} network_udp_t;

typedef network_udp_t *network_udp_handle_t;

esp_err_t network_udp_init(network_udp_handle_t *network_udp_handle_ptr,
                           network_events_handle_t events);
void network_udp_socket_close(network_udp_handle_t network_udp_handle);
void network_udp_free(network_udp_handle_t network_udp_handle);