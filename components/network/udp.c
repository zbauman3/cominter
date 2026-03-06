// Useful docs:
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/lwip.html#bsd-sockets-api

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>
#include <sys/param.h>

#include "network/messages.h"
#include "network/udp.h"

static const char *BASE_TAG = "NETWORK:UDP";
static const char *SOCKET_TAG = "NETWORK:UDP:SOCKET";
static const char *MULTICAST_WRITE_TAG = "NETWORK:UDP:MULTICAST:WRITE";
static const char *MULTICAST_READ_TAG = "NETWORK:UDP:MULTICAST:READ";
static const char *UDP_HEARTBEAT_TAG = "NETWORK:UDP:HEARTBEAT";

// // ----------------
// // Socket Stuff
// // ----------------

esp_err_t udp_socket_create(network_udp_handle_t network_udp_handle) {
  if (network_udp_handle->socket >= 0) {
    ESP_LOGW(SOCKET_TAG,
             "Multicast socket already created. Returning existing socket.");
    return network_udp_handle->socket;
  }

  struct sockaddr_in saddr = {0};
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};
  esp_err_t ret = ESP_OK;

  // Create the socket
  network_udp_handle->socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  ESP_GOTO_ON_FALSE(network_udp_handle->socket >= 0, ESP_ERR_INVALID_STATE,
                    udp_multicast_socket_create_end, SOCKET_TAG,
                    "Failed to create socket: %d", errno);

  // Bind the socket to the multicast port on any address
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(CONFIG_MULTICAST_PORT);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  ESP_GOTO_ON_FALSE(bind(network_udp_handle->socket, (struct sockaddr *)&saddr,
                         sizeof(struct sockaddr_in)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to bind socket: %d", errno);

  // Assign multicast TTL (set separately from normal interface TTL)
  uint8_t ttl = CONFIG_MULTICAST_TTL;
  ESP_GOTO_ON_FALSE(setsockopt(network_udp_handle->socket, IPPROTO_IP,
                               IP_MULTICAST_TTL, &ttl, sizeof(uint8_t)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_MULTICAST_TTL: %d", errno);

  // Configure the multicast source interface address
  inet_addr_from_ip4addr(&iaddr, &network_udp_handle->ip_info->ip);

  // Configure the address for multicast membership
  ESP_GOTO_ON_FALSE(
      inet_aton(CONFIG_MULTICAST_ADDR, &imreq.imr_multiaddr.s_addr) == 1,
      ESP_ERR_INVALID_ARG, udp_multicast_socket_create_end, SOCKET_TAG,
      "Multicast address '%s' is invalid", CONFIG_MULTICAST_ADDR);

  // Check if the multicast address is valid
  ESP_GOTO_ON_FALSE(!!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr)),
                    ESP_ERR_INVALID_ARG, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Address '%s' is not a valid multicast address",
                    CONFIG_MULTICAST_ADDR);

  ESP_LOGD(SOCKET_TAG, "Configured multicast address %s",
           inet_ntoa(imreq.imr_multiaddr.s_addr));

  // Assign the multicast source interface address
  ESP_GOTO_ON_FALSE(setsockopt(network_udp_handle->socket, IPPROTO_IP,
                               IP_MULTICAST_IF, &iaddr,
                               sizeof(struct in_addr)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_MULTICAST_IF: %d", errno);

  // Add the multicast group to the socket
  ESP_GOTO_ON_FALSE(setsockopt(network_udp_handle->socket, IPPROTO_IP,
                               IP_ADD_MEMBERSHIP, &imreq,
                               sizeof(struct ip_mreq)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_ADD_MEMBERSHIP: %d", errno);

  struct addrinfo hints = {
      .ai_flags = AI_PASSIVE,
      .ai_socktype = SOCK_DGRAM,
      .ai_family = AF_INET,
  };

  ESP_GOTO_ON_FALSE(getaddrinfo(CONFIG_MULTICAST_ADDR, NULL, &hints,
                                &network_udp_handle->multicast_addr_info) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to get multicast address info");

  ESP_GOTO_ON_FALSE(network_udp_handle->multicast_addr_info != 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "getaddrinfo() did not return any addresses");

  ((struct sockaddr_in *)network_udp_handle->multicast_addr_info->ai_addr)
      ->sin_port = htons(CONFIG_MULTICAST_PORT);

udp_multicast_socket_create_end:
  if (ret != ESP_OK) {
    if (network_udp_handle->socket >= 0) {
      close(network_udp_handle->socket);
      network_udp_handle->socket = -1;
    }
  }

  return ret;
}

void udp_socket_task(void *pvParameters) {
  network_udp_handle_t network_udp_handle = (network_udp_handle_t)pvParameters;
  esp_err_t create_status;

  while (true) {
    xEventGroupWaitBits(network_udp_handle->events->group_handle,
                        NETWORK_EVENT_GOT_NEW_IP, pdTRUE, pdTRUE,
                        portMAX_DELAY);
    ESP_LOGI(SOCKET_TAG, "Got new IP, creating socket...");

    network_udp_socket_close(network_udp_handle);

    while (network_udp_handle->socket < 0) {
      create_status = ESP_OK;
      create_status = udp_socket_create(network_udp_handle);

      if (create_status != ESP_OK || network_udp_handle->socket < 0) {
        ESP_LOGE(SOCKET_TAG, "Failed to create multicast socket. Retrying...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
      } else {
        ESP_LOGI(SOCKET_TAG, "Socket created successfully");
        // wait a bit to make sure the socket is ready
        vTaskDelay(pdMS_TO_TICKS(150));
        xEventGroupSetBits(network_udp_handle->events->group_handle,
                           NETWORK_EVENT_SOCKET_READY);
      }
    }
  }
}

void network_udp_heartbeat_task(void *pvParameters) {
  network_udp_handle_t network_udp_handle = (network_udp_handle_t)pvParameters;
  network_message_handle_t outgoing_message;
  BaseType_t xReturned;

  // when first coming on the network, send 10 heartbeats to make sure we're
  // put into peer lists.
  // ---------------------------------------------------------------------------
  // Need to work on this. Ideally this system would work:
  // - Every time the network comes online, send 6 heartbeats.
  // - Every time we receive a heartbeat from a peer, send 3 in response.
  // ---------------------------------------------------------------------------
  uint8_t init_heartbeat_count = 10;

  while (true) {
    if (network_message_init_heartbeat(
            &outgoing_message, network_udp_handle->state->device_info.name,
            network_udp_handle->mac_address) != ESP_OK) {
      ESP_LOGE(UDP_HEARTBEAT_TAG, "Failed to initialize message");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    xReturned = xQueueSendToBack(network_udp_handle->queues->message_outgoing,
                                 &outgoing_message, pdMS_TO_TICKS(5000));
    if (xReturned != pdPASS) {
      ESP_LOGE(UDP_HEARTBEAT_TAG,
               "Failed to send message to queue. Dropping message.");
      network_message_free(outgoing_message);
    }

    // the queue will now own the message.
    outgoing_message = NULL;

    // prune while we're here
    network_peers_prune(network_udp_handle->peers);

    if (init_heartbeat_count > 0) {
      init_heartbeat_count--;
      vTaskDelay(pdMS_TO_TICKS(NETWORK_UDP_HEARTBEAT_INIT_INTERVAL_MS));
    } else {
      vTaskDelay(pdMS_TO_TICKS(NETWORK_UDP_HEARTBEAT_INTERVAL_MS));
    }
  }
}

void network_udp_socket_close(network_udp_handle_t network_udp_handle) {
  if (network_udp_handle->socket >= 0) {
    shutdown(network_udp_handle->socket, SHUT_RDWR);
    close(network_udp_handle->socket);
  }
  xEventGroupClearBits(network_udp_handle->events->group_handle,
                       NETWORK_EVENT_SOCKET_READY);
  network_udp_handle->socket = -1;
}

// // ----------------
// // Multicast Stuff
// // ----------------

esp_err_t socket_receive_message(int socket, network_message_handle_t message) {
  // increase by 1 to check if the incoming message was longer than the max size
  // so that we can detect invalid messages easily by checking the length.
  uint8_t buffer[NETWORK_MESSAGE_MAX_LENGTH + 1];
  int length = 0;

  length = recv(socket, buffer, NETWORK_MESSAGE_MAX_LENGTH + 1, 0);

  if (length < 0) {
    ESP_LOGE(MULTICAST_READ_TAG, "multicast recvfrom failed: errno %d", errno);
    return ESP_ERR_INVALID_STATE;
  }

  if (length < sizeof(network_message_header_t)) {
    ESP_LOGE(MULTICAST_READ_TAG, "Message length too short: %d", length);
    return ESP_ERR_INVALID_STATE;
  }

  if (length > NETWORK_MESSAGE_MAX_LENGTH) {
    ESP_LOGE(MULTICAST_READ_TAG, "Message length too long: %d", length);
    return ESP_ERR_INVALID_STATE;
  }

  memcpy(&message->header, buffer, sizeof(network_message_header_t));

  int payload_len = length - sizeof(network_message_header_t);
  if (payload_len != message->header.length) {
    ESP_LOGE(MULTICAST_READ_TAG, "Payload length mismatch: expected %d, got %d",
             message->header.length, payload_len);
    return ESP_ERR_INVALID_STATE;
  }

  if (network_message_set_payload(
          message, buffer + sizeof(network_message_header_t)) != ESP_OK) {
    ESP_LOGE(MULTICAST_READ_TAG, "Failed to set payload");
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

esp_err_t socket_send_message(int socket, network_message_handle_t message,
                              struct addrinfo *addr_info) {
  uint8_t buffer[NETWORK_MESSAGE_MAX_LENGTH];
  int length = sizeof(network_message_header_t) + message->header.length;

  if (length > NETWORK_MESSAGE_MAX_LENGTH) {
    ESP_LOGE(MULTICAST_WRITE_TAG, "Message length too long: %d", length);
    return ESP_ERR_INVALID_ARG;
  }

  // first copy just the header data
  memcpy(buffer, &message->header, sizeof(network_message_header_t));

  // Then copy the body based on the type.
  // The body comes immediately after the header in the raw buffer.
  switch (message->header.type) {
  case MESSAGE_TYPE_TEXT:
    memcpy(buffer + sizeof(network_message_header_t), message->text.value,
           message->header.length);
    break;
  case MESSAGE_TYPE_AUDIO:
    memcpy(buffer + sizeof(network_message_header_t), message->audio.value,
           message->header.length);
    break;
  case MESSAGE_TYPE_HEARTBEAT:
    memcpy(buffer + sizeof(network_message_header_t),
           message->heartbeat.from_name, message->header.length);
    break;
  default:
    ESP_LOGE(MULTICAST_WRITE_TAG, "Unknown message type: %d",
             message->header.type);
    return ESP_ERR_INVALID_ARG;
  }

  if (sendto(socket, buffer, length, 0, addr_info->ai_addr,
             addr_info->ai_addrlen) < 0) {
    ESP_LOGE(MULTICAST_WRITE_TAG, "sendto failed: errno %d", errno);
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

void udp_multicast_read_task(void *pvParameters) {
  network_udp_handle_t network_udp_handle = (network_udp_handle_t)pvParameters;
  fd_set rfds;

  while (true) {
    xEventGroupWaitBits(network_udp_handle->events->group_handle,
                        NETWORK_EVENT_SOCKET_READY, pdFALSE, pdFALSE,
                        portMAX_DELAY);

    FD_ZERO(&rfds);
    FD_SET(network_udp_handle->socket, &rfds);
    // no timeout, so we'll block forever until data is received.
    int s = select(network_udp_handle->socket + 1, &rfds, NULL, NULL, NULL);

    // The socket could have been closed while waiting.
    // So we need to check if it's still open.
    EventBits_t bits_waiting =
        xEventGroupGetBits(network_udp_handle->events->group_handle);
    if (!(bits_waiting & NETWORK_EVENT_SOCKET_READY)) {
      ESP_LOGD(MULTICAST_READ_TAG, "Socket not ready, skipping read");
      continue;
    }

    if (s < 0) {
      ESP_LOGE(MULTICAST_READ_TAG, "Select failed: errno %d", errno);
      continue;
    }

    // no data
    if (s == 0) {
      ESP_LOGD(MULTICAST_READ_TAG, "No data received\n");
      continue;
    }

    // timeout – shouldn't happen?
    if (!FD_ISSET(network_udp_handle->socket, &rfds)) {
      continue;
    }

    // ----------------↓ TMP CODE ↓----------------
    network_message_handle_t incoming_message;
    if (network_message_init(&incoming_message, MESSAGE_TYPE_UNKNOWN, NULL,
                             NULL) != ESP_OK) {
      ESP_LOGE(MULTICAST_READ_TAG, "Failed to initialize message");
      continue;
    }
    // ----------------↑ TMP CODE ↑----------------

    if (socket_receive_message(network_udp_handle->socket, incoming_message) !=
        ESP_OK) {
      ESP_LOGE(MULTICAST_READ_TAG, "Failed to receive message");
      network_message_free(incoming_message);
      continue;
    }

    // ----------------↓ TMP CODE ↓----------------
    ESP_LOGI(
        MULTICAST_READ_TAG, "UUID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        incoming_message->header.uuid[0], incoming_message->header.uuid[1],
        incoming_message->header.uuid[2], incoming_message->header.uuid[3],
        incoming_message->header.uuid[4], incoming_message->header.uuid[5],
        incoming_message->header.uuid[6], incoming_message->header.uuid[7]);
    ESP_LOGI(MULTICAST_READ_TAG,
             "FROM MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             incoming_message->header.from_mac_address[0],
             incoming_message->header.from_mac_address[1],
             incoming_message->header.from_mac_address[2],
             incoming_message->header.from_mac_address[3],
             incoming_message->header.from_mac_address[4],
             incoming_message->header.from_mac_address[5]);
    ESP_LOGI(MULTICAST_READ_TAG,
             "TO MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             incoming_message->header.to_mac_address[0],
             incoming_message->header.to_mac_address[1],
             incoming_message->header.to_mac_address[2],
             incoming_message->header.to_mac_address[3],
             incoming_message->header.to_mac_address[4],
             incoming_message->header.to_mac_address[5]);

    network_peer_t *peer = network_udp_handle->peers->head;
    int peer_count = 0;
    while (peer != NULL) {
      peer_count++;
      peer = peer->next_peer;
    }

    ESP_LOGI(MULTICAST_READ_TAG, "Peer count: %d", peer_count);

    peer = NULL;
    peer = network_peers_find(network_udp_handle->peers,
                              incoming_message->header.from_mac_address, true);
    if (peer == NULL) {
      ESP_LOGI(MULTICAST_READ_TAG, "Peer not found");
    } else {
      ESP_LOGI(MULTICAST_READ_TAG, "Peer found: %s", peer->name);
    }

    // check if the `to_mac_address` is the same as the local MAC address
    // of if it is the broadcast MAC address.
    if (memcmp(incoming_message->header.to_mac_address,
               network_udp_handle->mac_address,
               sizeof(network_mac_address_t)) != 0 &&
        memcmp(incoming_message->header.to_mac_address,
               NETWORK_MESSAGE_BROADCAST_MAC_ADDRESS,
               sizeof(network_mac_address_t)) != 0) {
      ESP_LOGI(MULTICAST_READ_TAG, "Message is not for me, skipping");
      network_message_free(incoming_message);
      continue;
    }

    switch (incoming_message->header.type) {
    case MESSAGE_TYPE_TEXT:
      ESP_LOGI(MULTICAST_READ_TAG, "%s\n", incoming_message->text.value);
      break;
    case MESSAGE_TYPE_AUDIO:
      ESP_LOGI(MULTICAST_READ_TAG, "Audio message of length %d received\n",
               incoming_message->header.length);
      break;
    case MESSAGE_TYPE_HEARTBEAT:
      ESP_LOGI(MULTICAST_READ_TAG, "Heartbeat\n");
      network_peers_add(network_udp_handle->peers,
                        incoming_message->header.from_mac_address,
                        incoming_message->heartbeat.from_name);
      break;
    default:
      ESP_LOGE(MULTICAST_READ_TAG, "Unknown message type: %d\n",
               incoming_message->header.type);
      break;
    }

    network_message_free(incoming_message);
    // ----------------↑ TMP CODE ↑----------------
  }
}

void udp_multicast_write_task(void *pvParameters) {
  network_udp_handle_t network_udp_handle = (network_udp_handle_t)pvParameters;
  network_message_handle_t outgoing_message;

  while (true) {
    xEventGroupWaitBits(network_udp_handle->events->group_handle,
                        NETWORK_EVENT_SOCKET_READY, pdFALSE, pdFALSE,
                        portMAX_DELAY);

    // wait for the message queue to have a message
    BaseType_t xReturned =
        xQueueReceive(network_udp_handle->queues->message_outgoing,
                      &outgoing_message, portMAX_DELAY);

    // The socket could have been closed while waiting.
    // So we need to check if it's still open.
    EventBits_t bits_waiting =
        xEventGroupGetBits(network_udp_handle->events->group_handle);
    if (!(bits_waiting & NETWORK_EVENT_SOCKET_READY)) {
      ESP_LOGD(MULTICAST_WRITE_TAG, "Socket not ready, skipping write");

      xReturned = xQueueSendToFront(
          network_udp_handle->queues->message_outgoing, &outgoing_message, 0);
      if (xReturned == pdPASS) {
        // intentionally do not clean up the message here.
        // it was put back in the queue to be cleaned up by the next
        continue;
      }

      ESP_LOGE(MULTICAST_WRITE_TAG,
               "Failed to return message to queue. Dropping message.");
      goto udp_multicast_write_task_end;
    }

    if (xReturned != pdPASS) {
      ESP_LOGE(MULTICAST_WRITE_TAG, "Failed to receive message from queue");
      goto udp_multicast_write_task_end;
    }

    ESP_LOGI(
        MULTICAST_WRITE_TAG, "UUID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        outgoing_message->header.uuid[0], outgoing_message->header.uuid[1],
        outgoing_message->header.uuid[2], outgoing_message->header.uuid[3],
        outgoing_message->header.uuid[4], outgoing_message->header.uuid[5],
        outgoing_message->header.uuid[6], outgoing_message->header.uuid[7]);
    ESP_LOGI(MULTICAST_WRITE_TAG,
             "TO MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             outgoing_message->header.to_mac_address[0],
             outgoing_message->header.to_mac_address[1],
             outgoing_message->header.to_mac_address[2],
             outgoing_message->header.to_mac_address[3],
             outgoing_message->header.to_mac_address[4],
             outgoing_message->header.to_mac_address[5]);
    ESP_LOGI(MULTICAST_WRITE_TAG, "Message type: %d\n",
             outgoing_message->header.type);

    // if an address wasn't provided, use the local MAC address
    if (memcmp(outgoing_message->header.from_mac_address,
               NETWORK_MESSAGE_BROADCAST_MAC_ADDRESS,
               sizeof(network_mac_address_t)) == 0) {
      memcpy(outgoing_message->header.from_mac_address,
             network_udp_handle->mac_address, sizeof(network_mac_address_t));
    }

    if (socket_send_message(network_udp_handle->socket, outgoing_message,
                            network_udp_handle->multicast_addr_info) !=
        ESP_OK) {
      ESP_LOGE(MULTICAST_WRITE_TAG, "Failed to send message");
    }

  udp_multicast_write_task_end:
    network_message_free(outgoing_message);
    outgoing_message = NULL;
  }
}

// ----------------
// Setup Stuff
// ----------------

esp_err_t network_udp_init(network_udp_handle_t *network_udp_handle_ptr,
                           network_events_handle_t events_handle,
                           network_queues_handle_t queues_handle,
                           network_peers_list_handle_t peers_handle,
                           app_state_handle_t state_handle) {
  esp_err_t ret = ESP_OK;
  BaseType_t xReturned;

  network_udp_handle_t network_udp_handle =
      (network_udp_handle_t)malloc(sizeof(network_udp_t));
  ESP_GOTO_ON_FALSE(network_udp_handle != NULL, ESP_ERR_NO_MEM,
                    network_udp_init_error, BASE_TAG,
                    "Failed to allocate memory for network UDP handle");

  network_udp_handle->socket = -1;
  network_udp_handle->multicast_addr_info =
      (struct addrinfo *)malloc(sizeof(struct addrinfo));
  network_udp_handle->ip_info =
      (esp_netif_ip_info_t *)malloc(sizeof(esp_netif_ip_info_t));
  network_udp_handle->events = events_handle;
  network_udp_handle->queues = queues_handle;
  network_udp_handle->peers = peers_handle;
  network_udp_handle->state = state_handle;

  if (esp_read_mac(network_udp_handle->mac_address, ESP_MAC_WIFI_STA) !=
      ESP_OK) {
    ESP_LOGE(BASE_TAG, "Failed to read MAC address");
    ret = ESP_ERR_INVALID_STATE;
    goto network_udp_init_error;
  };

  xReturned =
      xTaskCreate(udp_multicast_write_task, MULTICAST_WRITE_TAG,
                  NETWORK_UDP_TASK_STACK_DEPTH_MULTICAST, network_udp_handle,
                  NETWORK_UDP_TASK_PRIORITY_MULTICAST,
                  &network_udp_handle->tasks.multicast_write);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast write task");
    ret = ESP_ERR_INVALID_STATE;
    goto network_udp_init_error;
  }
  if (network_udp_handle->tasks.multicast_write == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast write task");
    ret = ESP_ERR_NO_MEM;
    goto network_udp_init_error;
  }

  xReturned =
      xTaskCreate(udp_multicast_read_task, MULTICAST_READ_TAG,
                  NETWORK_UDP_TASK_STACK_DEPTH_MULTICAST, network_udp_handle,
                  NETWORK_UDP_TASK_PRIORITY_MULTICAST,
                  &network_udp_handle->tasks.multicast_read);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast read task");
    ret = ESP_ERR_INVALID_STATE;
    goto network_udp_init_error;
  }
  if (network_udp_handle->tasks.multicast_read == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast read task");
    ret = ESP_ERR_NO_MEM;
    goto network_udp_init_error;
  }

  xReturned = xTaskCreate(udp_socket_task, SOCKET_TAG,
                          NETWORK_UDP_TASK_STACK_DEPTH_SOCKET,
                          network_udp_handle, NETWORK_UDP_TASK_PRIORITY_SOCKET,
                          &network_udp_handle->tasks.socket);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create socket task");
    ret = ESP_ERR_INVALID_STATE;
    goto network_udp_init_error;
  }
  if (network_udp_handle->tasks.socket == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create socket task");
    ret = ESP_ERR_NO_MEM;
    goto network_udp_init_error;
  }

  xReturned =
      xTaskCreate(network_udp_heartbeat_task, UDP_HEARTBEAT_TAG,
                  APP_STATE_TASK_STACK_DEPTH_UDP_HEARTBEAT, network_udp_handle,
                  NETWORK_UDP_TASK_PRIORITY_UDP_HEARTBEAT,
                  &network_udp_handle->tasks.udp_heartbeat);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create UDP heartbeat task");
    ret = ESP_ERR_INVALID_STATE;
    goto network_udp_init_error;
  }
  if (network_udp_handle->tasks.udp_heartbeat == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create UDP heartbeat task");
    ret = ESP_ERR_NO_MEM;
    goto network_udp_init_error;
  }

  *network_udp_handle_ptr = network_udp_handle;

  return ESP_OK;

network_udp_init_error:
  free(network_udp_handle);
  return ret;
}

void network_udp_free(network_udp_handle_t network_udp_handle) {
  network_udp_socket_close(network_udp_handle);

  free(network_udp_handle->multicast_addr_info);
  free(network_udp_handle->ip_info);

  vTaskDelete(network_udp_handle->tasks.multicast_write);
  vTaskDelete(network_udp_handle->tasks.multicast_read);
  vTaskDelete(network_udp_handle->tasks.socket);
  vTaskDelete(network_udp_handle->tasks.udp_heartbeat);

  free(network_udp_handle);
}