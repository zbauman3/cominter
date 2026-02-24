// Useful docs:
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/lwip.html#bsd-sockets-api

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>
#include <sys/param.h>

#include "application/messages.h"
#include "network/udp.h"

static const char *BASE_TAG = "NETWORK:UDP";
static const char *SOCKET_TAG = "NETWORK:UDP:SOCKET";
static const char *MULTICAST_WRITE_TAG = "NETWORK:UDP:MULTICAST:WRITE";
static const char *MULTICAST_READ_TAG = "NETWORK:UDP:MULTICAST:READ";
static const char *UDP_HEARTBEAT_TAG = "NETWORK:UDP:HEARTBEAT";

// ----------------
// Socket Stuff
// ----------------

esp_err_t udp_socket_create(app_state_handle_t state_handle) {
  if (state_handle->network.socket >= 0) {
    ESP_LOGW(SOCKET_TAG,
             "Multicast socket already created. Returning existing socket.");
    return state_handle->network.socket;
  }

  struct sockaddr_in saddr = {0};
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};
  esp_err_t ret = ESP_OK;

  // Create the socket
  state_handle->network.socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  ESP_GOTO_ON_FALSE(state_handle->network.socket >= 0, ESP_ERR_INVALID_STATE,
                    udp_multicast_socket_create_end, SOCKET_TAG,
                    "Failed to create socket: %d", errno);

  // Bind the socket to the multicast port on any address
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(CONFIG_MULTICAST_PORT);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  ESP_GOTO_ON_FALSE(bind(state_handle->network.socket,
                         (struct sockaddr *)&saddr,
                         sizeof(struct sockaddr_in)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to bind socket: %d", errno);

  // Assign multicast TTL (set separately from normal interface TTL)
  uint8_t ttl = CONFIG_MULTICAST_TTL;
  ESP_GOTO_ON_FALSE(setsockopt(state_handle->network.socket, IPPROTO_IP,
                               IP_MULTICAST_TTL, &ttl, sizeof(uint8_t)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_MULTICAST_TTL: %d", errno);

  // Configure the multicast source interface address
  inet_addr_from_ip4addr(&iaddr, &state_handle->network.ip_info->ip);

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
  ESP_GOTO_ON_FALSE(setsockopt(state_handle->network.socket, IPPROTO_IP,
                               IP_MULTICAST_IF, &iaddr,
                               sizeof(struct in_addr)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_MULTICAST_IF: %d", errno);

  // Add the multicast group to the socket
  ESP_GOTO_ON_FALSE(setsockopt(state_handle->network.socket, IPPROTO_IP,
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
                                &state_handle->network.multicast_addr_info) >=
                        0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to get multicast address info");

  ESP_GOTO_ON_FALSE(state_handle->network.multicast_addr_info != 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "getaddrinfo() did not return any addresses");

  ((struct sockaddr_in *)state_handle->network.multicast_addr_info->ai_addr)
      ->sin_port = htons(CONFIG_MULTICAST_PORT);

udp_multicast_socket_create_end:
  if (ret != ESP_OK) {
    if (state_handle->network.socket >= 0) {
      close(state_handle->network.socket);
      state_handle->network.socket = -1;
    }
  }

  return ret;
}

void udp_socket_task(void *pvParameters) {
  app_state_handle_t state_handle = (app_state_handle_t)pvParameters;
  esp_err_t create_status;

  while (true) {
    xEventGroupWaitBits(state_handle->event_groups.network_events,
                        APP_STATE_NETWORK_EVENT_GOT_NEW_IP, pdTRUE, pdTRUE,
                        portMAX_DELAY);
    ESP_LOGI(SOCKET_TAG, "Got new IP, creating socket...");

    network_udp_socket_close(state_handle);

    while (state_handle->network.socket < 0) {
      create_status = ESP_OK;
      create_status = udp_socket_create(state_handle);

      if (create_status != ESP_OK || state_handle->network.socket < 0) {
        ESP_LOGE(SOCKET_TAG, "Failed to create multicast socket. Retrying...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
      } else {
        ESP_LOGI(SOCKET_TAG, "Socket created successfully");
        // wait a bit to make sure the socket is ready
        vTaskDelay(pdMS_TO_TICKS(150));
        xEventGroupSetBits(state_handle->event_groups.network_events,
                           APP_STATE_NETWORK_EVENT_SOCKET_READY);
      }
    }
  }
}

void network_udp_heartbeat_task(void *pvParameters) {
  app_state_handle_t state_handle = (app_state_handle_t)pvParameters;
  app_message_handle_t outgoing_message;
  BaseType_t xReturned;

  while (true) {
    if (app_message_init_heartbeat(state_handle, &outgoing_message) != ESP_OK) {
      ESP_LOGE(UDP_HEARTBEAT_TAG, "Failed to initialize message");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    xReturned = xQueueSendToBack(state_handle->queues.message_outgoing_queue,
                                 &outgoing_message, pdMS_TO_TICKS(5000));
    if (xReturned != pdPASS) {
      ESP_LOGE(UDP_HEARTBEAT_TAG,
               "Failed to send message to queue. Dropping message.");
      app_message_free(outgoing_message);
    }

    // the queue will now own the message.
    outgoing_message = NULL;

    // prune while we're here
    app_state_peers_prune(state_handle);

    vTaskDelay(pdMS_TO_TICKS(NETWORK_UDP_HEARTBEAT_INTERVAL_MS));
  }
}

void network_udp_socket_close(app_state_handle_t state_handle) {
  if (state_handle->network.socket >= 0) {
    shutdown(state_handle->network.socket, SHUT_RDWR);
    close(state_handle->network.socket);
  }
  xEventGroupClearBits(state_handle->event_groups.network_events,
                       APP_STATE_NETWORK_EVENT_SOCKET_READY);
  state_handle->network.socket = -1;
}

// ----------------
// Multicast Stuff
// ----------------

esp_err_t socket_receive_message(int socket, app_message_handle_t message) {
  // increase by 1 to check if the incoming message was longer than the max size
  // so that we can detect invalid messages easily by checking the length.
  uint8_t buffer[APP_MESSAGE_MAX_LENGTH + 1];
  int length = 0;

  length = recv(socket, buffer, APP_MESSAGE_MAX_LENGTH + 1, 0);

  if (length < 0) {
    ESP_LOGE(MULTICAST_READ_TAG, "multicast recvfrom failed: errno %d", errno);
    return ESP_ERR_INVALID_STATE;
  }

  if (length < sizeof(app_message_header_t)) {
    ESP_LOGE(MULTICAST_READ_TAG, "Message length too short: %d", length);
    return ESP_ERR_INVALID_STATE;
  }

  if (length > APP_MESSAGE_MAX_LENGTH) {
    ESP_LOGE(MULTICAST_READ_TAG, "Message length too long: %d", length);
    return ESP_ERR_INVALID_STATE;
  }

  memcpy(&message->header, buffer, sizeof(app_message_header_t));

  int payload_len = length - sizeof(app_message_header_t);
  if (payload_len != message->header.length) {
    ESP_LOGE(MULTICAST_READ_TAG, "Payload length mismatch: expected %d, got %d",
             message->header.length, payload_len);
    return ESP_ERR_INVALID_STATE;
  }

  if (app_message_set_payload(message, buffer + sizeof(app_message_header_t)) !=
      ESP_OK) {
    ESP_LOGE(MULTICAST_READ_TAG, "Failed to set payload");
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

esp_err_t socket_send_message(int socket, app_message_handle_t message,
                              struct addrinfo *addr_info) {
  uint8_t buffer[APP_MESSAGE_MAX_LENGTH];
  int length = sizeof(app_message_header_t) + message->header.length;

  if (length > APP_MESSAGE_MAX_LENGTH) {
    ESP_LOGE(MULTICAST_WRITE_TAG, "Message length too long: %d", length);
    return ESP_ERR_INVALID_ARG;
  }

  // first copy just the header data
  memcpy(buffer, &message->header, sizeof(app_message_header_t));

  // Then copy the body based on the type.
  // The body comes immediately after the header in the raw buffer.
  switch (message->header.type) {
  case MESSAGE_TYPE_TEXT:
    memcpy(buffer + sizeof(app_message_header_t), message->text.value,
           message->header.length);
    break;
  case MESSAGE_TYPE_AUDIO:
    memcpy(buffer + sizeof(app_message_header_t), message->audio.value,
           message->header.length);
    break;
  case MESSAGE_TYPE_HEARTBEAT:
    memcpy(buffer + sizeof(app_message_header_t), message->heartbeat.name,
           message->header.length);
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
  app_state_handle_t state_handle = (app_state_handle_t)pvParameters;
  fd_set rfds;

  while (true) {
    xEventGroupWaitBits(state_handle->event_groups.network_events,
                        APP_STATE_NETWORK_EVENT_SOCKET_READY, pdFALSE, pdFALSE,
                        portMAX_DELAY);

    FD_ZERO(&rfds);
    FD_SET(state_handle->network.socket, &rfds);
    // no timeout, so we'll block forever until data is received.
    int s = select(state_handle->network.socket + 1, &rfds, NULL, NULL, NULL);

    // The socket could have been closed while waiting.
    // So we need to check if it's still open.
    EventBits_t bits_waiting =
        xEventGroupGetBits(state_handle->event_groups.network_events);
    if (!(bits_waiting & APP_STATE_NETWORK_EVENT_SOCKET_READY)) {
      ESP_LOGD(MULTICAST_READ_TAG, "Socket not ready, skipping read");
      continue;
    }

    if (s < 0) {
      ESP_LOGE(MULTICAST_READ_TAG, "Select failed: errno %d", errno);
      continue;
    }

    ESP_LOGI(MULTICAST_READ_TAG, "----%s Receive----",
             state_handle->device_info.name);

    // no data
    if (s == 0) {
      ESP_LOGD(MULTICAST_READ_TAG, "No data received\n");
      continue;
    }

    // timeout – shouldn't happen?
    if (!FD_ISSET(state_handle->network.socket, &rfds)) {
      continue;
    }

    // ----------------↓ TMP CODE ↓----------------
    app_message_handle_t incoming_message;
    if (app_message_init(state_handle, &incoming_message, MESSAGE_TYPE_UNKNOWN,
                         NULL) != ESP_OK) {
      ESP_LOGE(MULTICAST_READ_TAG, "Failed to initialize message");
      continue;
    }
    // ----------------↑ TMP CODE ↑----------------

    if (socket_receive_message(state_handle->network.socket,
                               incoming_message) != ESP_OK) {
      ESP_LOGE(MULTICAST_READ_TAG, "Failed to receive message");
      app_message_free(incoming_message);
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

    app_state_peer_t *peer = state_handle->peers.list;
    int peer_count = 0;
    while (peer != NULL) {
      peer_count++;
      peer = peer->next_peer;
    }

    ESP_LOGI(MULTICAST_READ_TAG, "Peer count: %d", peer_count);

    peer = NULL;
    peer = app_state_peer_find(state_handle,
                               incoming_message->header.from_mac_address, true);
    if (peer == NULL) {
      ESP_LOGI(MULTICAST_READ_TAG, "Peer not found");
    } else {
      ESP_LOGI(MULTICAST_READ_TAG, "Peer found: %s", peer->name);
    }

    // check if the `to_mac_address` is the same as the local MAC address
    // of if it is the broadcast MAC address.
    if (memcmp(incoming_message->header.to_mac_address,
               state_handle->device_info.mac_address, 6) != 0 &&
        memcmp(incoming_message->header.to_mac_address,
               APP_MESSAGE_BROADCAST_MAC_ADDRESS, 6) != 0) {
      ESP_LOGI(MULTICAST_READ_TAG, "Message is not for me, skipping");
      app_message_free(incoming_message);
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
      app_state_peer_add(state_handle,
                         incoming_message->header.from_mac_address,
                         incoming_message->heartbeat.name);
      break;
    default:
      ESP_LOGE(MULTICAST_READ_TAG, "Unknown message type: %d\n",
               incoming_message->header.type);
      break;
    }

    app_message_free(incoming_message);
    // ----------------↑ TMP CODE ↑----------------
  }
}

void udp_multicast_write_task(void *pvParameters) {
  app_state_handle_t state_handle = (app_state_handle_t)pvParameters;
  app_message_handle_t outgoing_message;

  while (true) {
    xEventGroupWaitBits(state_handle->event_groups.network_events,
                        APP_STATE_NETWORK_EVENT_SOCKET_READY, pdFALSE, pdFALSE,
                        portMAX_DELAY);

    // wait for the message queue to have a message
    BaseType_t xReturned =
        xQueueReceive(state_handle->queues.message_outgoing_queue,
                      &outgoing_message, portMAX_DELAY);

    // The socket could have been closed while waiting.
    // So we need to check if it's still open.
    EventBits_t bits_waiting =
        xEventGroupGetBits(state_handle->event_groups.network_events);
    if (!(bits_waiting & APP_STATE_NETWORK_EVENT_SOCKET_READY)) {
      ESP_LOGD(MULTICAST_WRITE_TAG, "Socket not ready, skipping write");

      xReturned = xQueueSendToFront(state_handle->queues.message_outgoing_queue,
                                    &outgoing_message, 0);
      if (xReturned == pdPASS) {
        // intentionally do not clean up the message here.
        // it was put back in the queue to be cleaned up by the next iteration.
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

    ESP_LOGI(MULTICAST_WRITE_TAG, "----%s Send----",
             state_handle->device_info.name);
    ESP_LOGI(
        MULTICAST_WRITE_TAG, "UUID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        outgoing_message->header.uuid[0], outgoing_message->header.uuid[1],
        outgoing_message->header.uuid[2], outgoing_message->header.uuid[3],
        outgoing_message->header.uuid[4], outgoing_message->header.uuid[5],
        outgoing_message->header.uuid[6], outgoing_message->header.uuid[7]);
    ESP_LOGI(MULTICAST_WRITE_TAG,
             "FROM MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             outgoing_message->header.from_mac_address[0],
             outgoing_message->header.from_mac_address[1],
             outgoing_message->header.from_mac_address[2],
             outgoing_message->header.from_mac_address[3],
             outgoing_message->header.from_mac_address[4],
             outgoing_message->header.from_mac_address[5]);
    ESP_LOGI(MULTICAST_WRITE_TAG,
             "TO MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
             outgoing_message->header.to_mac_address[0],
             outgoing_message->header.to_mac_address[1],
             outgoing_message->header.to_mac_address[2],
             outgoing_message->header.to_mac_address[3],
             outgoing_message->header.to_mac_address[4],
             outgoing_message->header.to_mac_address[5]);

    if (socket_send_message(state_handle->network.socket, outgoing_message,
                            state_handle->network.multicast_addr_info) !=
        ESP_OK) {
      ESP_LOGE(MULTICAST_WRITE_TAG, "Failed to send message");
    }

  udp_multicast_write_task_end:
    app_message_free(outgoing_message);
    outgoing_message = NULL;
  }
}

// ----------------
// Setup Stuff
// ----------------

esp_err_t network_udp_init(app_state_handle_t state_handle) {
  BaseType_t xReturned;

  xReturned = xTaskCreate(udp_multicast_write_task, MULTICAST_WRITE_TAG,
                          APP_STATE_TASK_STACK_DEPTH_MULTICAST, state_handle,
                          APP_STATE_TASK_PRIORITY_MULTICAST,
                          &state_handle->tasks.multicast_write_task);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast write task");
    return ESP_ERR_INVALID_STATE;
  }
  if (state_handle->tasks.multicast_write_task == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast write task");
    return ESP_ERR_NO_MEM;
  }

  xReturned = xTaskCreate(udp_multicast_read_task, MULTICAST_READ_TAG,
                          APP_STATE_TASK_STACK_DEPTH_MULTICAST, state_handle,
                          APP_STATE_TASK_PRIORITY_MULTICAST,
                          &state_handle->tasks.multicast_read_task);
  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast read task");
    return ESP_ERR_INVALID_STATE;
  }
  if (state_handle->tasks.multicast_read_task == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast read task");
    return ESP_ERR_NO_MEM;
  }

  xReturned = xTaskCreate(udp_socket_task, SOCKET_TAG,
                          APP_STATE_TASK_STACK_DEPTH_SOCKET, state_handle,
                          APP_STATE_TASK_PRIORITY_SOCKET,
                          &state_handle->tasks.socket_task);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create socket task");
    return ESP_ERR_INVALID_STATE;
  }
  if (state_handle->tasks.socket_task == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create socket task");
    return ESP_ERR_NO_MEM;
  }

  xReturned = xTaskCreate(network_udp_heartbeat_task, UDP_HEARTBEAT_TAG,
                          APP_STATE_TASK_STACK_DEPTH_UDP_HEARTBEAT,
                          state_handle, APP_STATE_TASK_PRIORITY_UDP_HEARTBEAT,
                          &state_handle->tasks.udp_heartbeat_task);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create UDP heartbeat task");
    return ESP_ERR_INVALID_STATE;
  }
  if (state_handle->tasks.udp_heartbeat_task == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create UDP heartbeat task");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}