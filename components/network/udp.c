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

  if (network_udp_handle->multicast_addr_info != NULL) {
    freeaddrinfo(network_udp_handle->multicast_addr_info);
    network_udp_handle->multicast_addr_info = NULL;
  }

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
    ESP_LOGD(SOCKET_TAG, "Got new IP, creating socket...");

    network_udp_socket_close(network_udp_handle);

    while (network_udp_handle->socket < 0) {
      create_status = ESP_OK;
      create_status = udp_socket_create(network_udp_handle);

      if (create_status != ESP_OK || network_udp_handle->socket < 0) {
        ESP_LOGE(SOCKET_TAG, "Failed to create multicast socket. Retrying...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
      } else {
        ESP_LOGD(SOCKET_TAG, "Socket created successfully");
        // wait a bit to make sure the socket is ready
        vTaskDelay(pdMS_TO_TICKS(150));
        xEventGroupSetBits(network_udp_handle->events->group_handle,
                           NETWORK_EVENT_SOCKET_READY);
      }
    }
  }
}

void network_udp_socket_close(network_udp_handle_t network_udp_handle) {
  if (network_udp_handle->socket >= 0) {
    shutdown(network_udp_handle->socket, SHUT_RDWR);
    close(network_udp_handle->socket);
  }
  if (network_udp_handle->multicast_addr_info != NULL) {
    freeaddrinfo(network_udp_handle->multicast_addr_info);
    network_udp_handle->multicast_addr_info = NULL;
  }
  xEventGroupClearBits(network_udp_handle->events->group_handle,
                       NETWORK_EVENT_SOCKET_READY);
  network_udp_handle->socket = -1;
}

// // ----------------
// // Multicast Stuff
// // ----------------

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
    memcpy(buffer + sizeof(app_message_header_t), message->heartbeat.from_name,
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
  network_udp_handle_t network_udp_handle = (network_udp_handle_t)pvParameters;
  fd_set rfds;
  app_message_handle_t message_incoming = NULL;

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

    // TODO: If we can remove this usage of `device_info` then we don't need to
    // depend on `device_info` at all
    if (app_message_init(&message_incoming, MESSAGE_TYPE_UNKNOWN,
                         network_udp_handle->device_info->mac_address,
                         NULL) != ESP_OK) {
      ESP_LOGE(MULTICAST_READ_TAG, "Failed to initialize message");
      message_incoming = NULL;
      continue;
    }

    if (socket_receive_message(network_udp_handle->socket, message_incoming) !=
        ESP_OK) {
      ESP_LOGE(MULTICAST_READ_TAG, "Failed to receive message");
      app_message_free(message_incoming);
      message_incoming = NULL;
      continue;
    }

    ESP_LOGD(
        MULTICAST_READ_TAG, "UUID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        message_incoming->header.uuid[0], message_incoming->header.uuid[1],
        message_incoming->header.uuid[2], message_incoming->header.uuid[3],
        message_incoming->header.uuid[4], message_incoming->header.uuid[5],
        message_incoming->header.uuid[6], message_incoming->header.uuid[7]);
    ESP_LOGD(MULTICAST_READ_TAG,
             "FROM MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             message_incoming->header.from_mac_address[0],
             message_incoming->header.from_mac_address[1],
             message_incoming->header.from_mac_address[2],
             message_incoming->header.from_mac_address[3],
             message_incoming->header.from_mac_address[4],
             message_incoming->header.from_mac_address[5]);
    ESP_LOGD(MULTICAST_READ_TAG, "Message type: %d\n",
             message_incoming->header.type);

    xQueueSendToBack(network_udp_handle->queues->message_incoming,
                     &message_incoming, pdMS_TO_TICKS(1000));

    // the queue will now own the message.
    message_incoming = NULL;
  }
}

void udp_multicast_write_task(void *pvParameters) {
  network_udp_handle_t network_udp_handle = (network_udp_handle_t)pvParameters;
  app_message_handle_t message_outgoing;

  while (true) {
    xEventGroupWaitBits(network_udp_handle->events->group_handle,
                        NETWORK_EVENT_SOCKET_READY, pdFALSE, pdFALSE,
                        portMAX_DELAY);

    // wait for the message queue to have a message
    BaseType_t xReturned =
        xQueueReceive(network_udp_handle->queues->message_outgoing,
                      &message_outgoing, portMAX_DELAY);

    // The socket could have been closed while waiting.
    // So we need to check if it's still open.
    EventBits_t bits_waiting =
        xEventGroupGetBits(network_udp_handle->events->group_handle);
    if (!(bits_waiting & NETWORK_EVENT_SOCKET_READY)) {
      ESP_LOGD(MULTICAST_WRITE_TAG, "Socket not ready, skipping write");

      xReturned = xQueueSendToFront(
          network_udp_handle->queues->message_outgoing, &message_outgoing, 0);
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

    ESP_LOGD(
        MULTICAST_WRITE_TAG, "UUID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        message_outgoing->header.uuid[0], message_outgoing->header.uuid[1],
        message_outgoing->header.uuid[2], message_outgoing->header.uuid[3],
        message_outgoing->header.uuid[4], message_outgoing->header.uuid[5],
        message_outgoing->header.uuid[6], message_outgoing->header.uuid[7]);
    ESP_LOGD(MULTICAST_WRITE_TAG,
             "TO MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             message_outgoing->header.to_mac_address[0],
             message_outgoing->header.to_mac_address[1],
             message_outgoing->header.to_mac_address[2],
             message_outgoing->header.to_mac_address[3],
             message_outgoing->header.to_mac_address[4],
             message_outgoing->header.to_mac_address[5]);
    ESP_LOGD(MULTICAST_WRITE_TAG, "Message type: %d\n",
             message_outgoing->header.type);

    if (socket_send_message(network_udp_handle->socket, message_outgoing,
                            network_udp_handle->multicast_addr_info) !=
        ESP_OK) {
      ESP_LOGE(MULTICAST_WRITE_TAG, "Failed to send message");
    }

  udp_multicast_write_task_end:
    app_message_free(message_outgoing);
    message_outgoing = NULL;
  }
}

// ----------------
// Setup Stuff
// ----------------

esp_err_t network_udp_init(network_udp_handle_t *network_udp_handle_ptr,
                           network_events_handle_t events_handle,
                           app_queues_handle_t queues_handle,
                           app_device_info_handle_t device_info_handle) {
  esp_err_t ret = ESP_OK;
  BaseType_t xReturned;

  network_udp_handle_t network_udp_handle =
      (network_udp_handle_t)malloc(sizeof(network_udp_t));
  ESP_GOTO_ON_FALSE(network_udp_handle != NULL, ESP_ERR_NO_MEM,
                    network_udp_init_error, BASE_TAG,
                    "Failed to allocate memory for network UDP handle");

  network_udp_handle->socket = -1;
  network_udp_handle->multicast_addr_info = NULL;
  network_udp_handle->ip_info =
      (esp_netif_ip_info_t *)malloc(sizeof(esp_netif_ip_info_t));
  network_udp_handle->events = events_handle;
  network_udp_handle->queues = queues_handle;
  network_udp_handle->device_info = device_info_handle;

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

  *network_udp_handle_ptr = network_udp_handle;

  return ESP_OK;

network_udp_init_error:
  return ret;
}