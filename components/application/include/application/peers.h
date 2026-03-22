#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "application/device_info.h"
#include "application/queues.h"
#include "protocols/mac.h"

#define APP_PEERS_TASK_PRIORITY_HEARTBEAT 3
#define APP_PEERS_TASK_STACK_DEPTH_HEARTBEAT (1024 * 4)

#define APP_PEERS_PRUNE_INTERVAL_MS 60000 // 1 minute
#define APP_PEERS_HEARTBEAT_INTERVAL_MS 10000
#define APP_PEERS_HEARTBEAT_INIT_INTERVAL_MS 1000
#define APP_PEERS_HEARTBEAT_WAIT_MAX_MS (APP_PEERS_PRUNE_INTERVAL_MS / 2)

// This is a linked list of peers.
typedef struct app_peer_t {
  protocol_mac_address_t mac_address;
  char *name;
  int32_t last_heartbeat_ms;
  struct app_peer_t *next_peer;
} app_peer_t;

typedef app_peer_t *app_peer_handle_t;

typedef struct app_peers_list_t {
  app_peer_handle_t head;
  SemaphoreHandle_t mutex;
} app_peers_list_t;

typedef struct app_peers_t {
  app_peers_list_t list;
  struct {
    TaskHandle_t heartbeat_send;
    TaskHandle_t heartbeat_receive;
  } tasks;
  app_device_info_handle_t device_info;
  app_queues_handle_t queues;
} app_peers_t;

typedef app_peers_t *app_peers_handle_t;

esp_err_t app_peers_init(app_peers_handle_t *peers_handle_ptr,
                         app_device_info_handle_t device_info_handle,
                         app_queues_handle_t queues_handle);

esp_err_t app_peers_add(app_peers_handle_t peers_handle,
                        protocol_mac_address_t mac_address, char *name);
void app_peers_find(app_peers_handle_t peers_handle,
                    app_peer_handle_t *peer_handle_ptr,
                    protocol_mac_address_t mac_address, bool should_lock);
int32_t app_peers_count(app_peers_handle_t peers_handle);
void app_peer_free(app_peer_handle_t peer_handle);