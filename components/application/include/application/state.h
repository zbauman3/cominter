#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <lwip/netdb.h>

#define APP_STATE_TASK_PRIORITY_SOCKET 6
#define APP_STATE_TASK_PRIORITY_MULTICAST 5
#define APP_STATE_TASK_PRIORITY_INPUTS 4
#define APP_STATE_TASK_PRIORITY_UDP_HEARTBEAT 3

#define APP_STATE_TASK_STACK_DEPTH_SOCKET ((1024 * 3) + APP_MESSAGE_MAX_LENGTH)
#define APP_STATE_TASK_STACK_DEPTH_MULTICAST                                   \
  ((1024 * 3) + APP_MESSAGE_MAX_LENGTH)
#define APP_STATE_TASK_STACK_DEPTH_INPUTS 1024 * 2
#define APP_STATE_TASK_STACK_DEPTH_UDP_HEARTBEAT 1024

#define APP_STATE_NETWORK_EVENT_GOT_NEW_IP (1 << 0)
#define APP_STATE_NETWORK_EVENT_SOCKET_READY (1 << 1)

typedef uint8_t app_state_mac_address_t[6];

// This is a linked list of peers.
typedef struct app_state_peer_t {
  app_state_mac_address_t mac_address;
  char *name;
  int32_t last_heartbeat_ms;
  struct app_state_peer_t *next_peer;
} app_state_peer_t;

typedef struct app_state_t {
  struct {
    char *name;
    uint8_t mac_address[6];
  } device_info;
  struct {
    app_state_peer_t *list;
    SemaphoreHandle_t peers_mutex;
  } peers;
  struct {
    int socket;
    struct addrinfo *multicast_addr_info;
    esp_netif_ip_info_t *ip_info;
  } network;
  struct {
    TaskHandle_t socket_task;
    TaskHandle_t multicast_read_task;
    TaskHandle_t multicast_write_task;
    TaskHandle_t inputs_task;
    TaskHandle_t udp_heartbeat_task;
  } tasks;
  struct {
    EventGroupHandle_t network_events;
  } event_groups;
  struct {
    QueueHandle_t inputs_queue;
    // This contains a pointer to a message.
    // Readers must free the message after use.
    // Writers must not interact with the message.
    QueueHandle_t message_outgoing_queue;
  } queues;
  struct {
    int talk_btn;
  } pins;
} app_state_t;

typedef app_state_t *app_state_handle_t;

esp_err_t app_state_init(app_state_handle_t *handle, int talk_btn_pin);

esp_err_t app_state_peer_add(app_state_handle_t state_handle,
                             app_state_mac_address_t mac_address, char *name);
esp_err_t app_state_peer_remove(app_state_handle_t state_handle,
                                app_state_mac_address_t mac_address);
esp_err_t app_state_peers_prune(app_state_handle_t state_handle);
app_state_peer_t *app_state_peer_find(app_state_handle_t state_handle,
                                      app_state_mac_address_t mac_address,
                                      bool should_lock);