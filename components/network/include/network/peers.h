#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "network/types.h"

// This is a linked list of peers.
typedef struct network_peer_t {
  network_mac_address_t mac_address;
  char *name;
  int32_t last_heartbeat_ms;
  struct network_peer_t *next_peer;
} network_peer_t;

typedef struct network_peers_list_t {
  network_peer_t *head;
  SemaphoreHandle_t mutex;
} network_peers_list_t;

typedef network_peers_list_t *network_peers_list_handle_t;

esp_err_t
network_peers_init(network_peers_list_handle_t *peers_list_handle_ptr);

esp_err_t network_peers_add(network_peers_list_handle_t peers_list_handle,
                            network_mac_address_t mac_address, char *name);
esp_err_t network_peers_remove(network_peers_list_handle_t peers_list_handle,
                               network_mac_address_t mac_address);
esp_err_t network_peers_prune(network_peers_list_handle_t peers_list_handle);
network_peer_t *
network_peers_find(network_peers_list_handle_t peers_list_handle,
                   network_mac_address_t mac_address, bool should_lock);