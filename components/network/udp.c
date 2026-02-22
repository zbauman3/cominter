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
static const char *MULTICAST_TAG = "NETWORK:UDP:MULTICAST";

// ----------------
// Socket Stuff
// ----------------

esp_err_t udp_socket_create(state_handle_t state_handle) {
  if (state_handle->socket >= 0) {
    ESP_LOGW(SOCKET_TAG,
             "Multicast socket already created. Returning existing socket.");
    return state_handle->socket;
  }

  struct sockaddr_in saddr = {0};
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};
  esp_err_t ret = ESP_OK;

  // Create the socket
  state_handle->socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  ESP_GOTO_ON_FALSE(state_handle->socket >= 0, ESP_ERR_INVALID_STATE,
                    udp_multicast_socket_create_end, SOCKET_TAG,
                    "Failed to create socket: %d", errno);

  // Bind the socket to the multicast port on any address
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(CONFIG_MULTICAST_PORT);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  ESP_GOTO_ON_FALSE(bind(state_handle->socket, (struct sockaddr *)&saddr,
                         sizeof(struct sockaddr_in)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to bind socket: %d", errno);

  // Assign multicast TTL (set separately from normal interface TTL)
  uint8_t ttl = CONFIG_MULTICAST_TTL;
  ESP_GOTO_ON_FALSE(setsockopt(state_handle->socket, IPPROTO_IP,
                               IP_MULTICAST_TTL, &ttl, sizeof(uint8_t)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_MULTICAST_TTL: %d", errno);

  // Configure the multicast source interface address
  inet_addr_from_ip4addr(&iaddr, &state_handle->ip_info->ip);

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
  ESP_GOTO_ON_FALSE(setsockopt(state_handle->socket, IPPROTO_IP,
                               IP_MULTICAST_IF, &iaddr,
                               sizeof(struct in_addr)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_MULTICAST_IF: %d", errno);

  // Add the multicast group to the socket
  ESP_GOTO_ON_FALSE(setsockopt(state_handle->socket, IPPROTO_IP,
                               IP_ADD_MEMBERSHIP, &imreq,
                               sizeof(struct ip_mreq)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_ADD_MEMBERSHIP: %d", errno);

udp_multicast_socket_create_end:
  if (ret != ESP_OK) {
    if (state_handle->socket >= 0) {
      close(state_handle->socket);
      state_handle->socket = -1;
    }
  }

  return ret;
}

void udp_socket_task(void *pvParameters) {
  state_handle_t state_handle = (state_handle_t)pvParameters;
  esp_err_t create_status;

  while (true) {
    xEventGroupWaitBits(state_handle->network_events,
                        STATE_NETWORK_EVENT_GOT_NEW_IP, pdTRUE, pdTRUE,
                        portMAX_DELAY);
    ESP_LOGI(SOCKET_TAG, "Got new IP, creating socket...");

    udp_socket_close(state_handle);

    while (state_handle->socket < 0) {
      create_status = ESP_OK;
      create_status = udp_socket_create(state_handle);

      if (create_status != ESP_OK || state_handle->socket < 0) {
        ESP_LOGE(SOCKET_TAG, "Failed to create multicast socket. Retrying...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
      } else {
        ESP_LOGI(SOCKET_TAG, "Socket created successfully");
        xEventGroupSetBits(state_handle->network_events,
                           STATE_NETWORK_EVENT_SOCKET_READY);
      }
    }
  }
}

void udp_socket_close(state_handle_t state_handle) {
  if (state_handle->socket >= 0) {
    shutdown(state_handle->socket, SHUT_RDWR);
    close(state_handle->socket);
  }
  xEventGroupClearBits(state_handle->network_events,
                       STATE_NETWORK_EVENT_SOCKET_READY);
  state_handle->socket = -1;
}

// ----------------
// Multicast Stuff
// ----------------

esp_err_t socket_receive_message(int socket, message_handle_t message) {
  // increase by 1 to check if the incoming message was longer than the max size
  // so that we can detect invalid messages easily by checking the length.
  uint8_t buffer[MESSAGE_MAX_LENGTH + 1];
  int length = 0;

  length = recv(socket, buffer, MESSAGE_MAX_LENGTH + 1, 0);

  if (length < 0) {
    ESP_LOGE(MULTICAST_TAG, "multicast recvfrom failed: errno %d", errno);
    return ESP_ERR_INVALID_STATE;
  }

  if (length < sizeof(message_header_t)) {
    ESP_LOGE(MULTICAST_TAG, "Message length too short: %d", length);
    return ESP_ERR_INVALID_STATE;
  }

  if (length > MESSAGE_MAX_LENGTH) {
    ESP_LOGE(MULTICAST_TAG, "Message length too long: %d", length);
    return ESP_ERR_INVALID_STATE;
  }

  memcpy(&message->header, buffer, sizeof(message_header_t));

  int payload_len = length - sizeof(message_header_t);
  if (payload_len != message->header.length) {
    ESP_LOGE(MULTICAST_TAG, "Payload length mismatch: expected %d, got %d",
             message->header.length, payload_len);
    return ESP_ERR_INVALID_STATE;
  }

  if (message_set_payload(message, buffer + sizeof(message_header_t)) !=
      ESP_OK) {
    ESP_LOGE(MULTICAST_TAG, "Failed to set payload");
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

esp_err_t socket_send_message(int socket, message_handle_t message,
                              struct addrinfo *addr_info) {
  uint8_t buffer[MESSAGE_MAX_LENGTH];
  int length = sizeof(message_header_t) + message->header.length;

  if (length > MESSAGE_MAX_LENGTH) {
    ESP_LOGE(MULTICAST_TAG, "Message length too long: %d", length);
    return ESP_ERR_INVALID_ARG;
  }

  // first copy just the header data
  memcpy(buffer, &message->header, sizeof(message_header_t));

  // Then copy the body based on the type.
  // The body comes immediately after the header in the raw buffer.
  switch (message->header.type) {
  case MESSAGE_TYPE_TEXT:
    memcpy(buffer + sizeof(message_header_t), message->text.value,
           message->header.length);
    break;
  case MESSAGE_TYPE_AUDIO:
    memcpy(buffer + sizeof(message_header_t), message->audio.value,
           message->header.length);
    break;
  default:
    ESP_LOGE(MULTICAST_TAG, "Unknown message type: %d", message->header.type);
    return ESP_ERR_INVALID_ARG;
  }

  if (sendto(socket, buffer, length, 0, addr_info->ai_addr,
             addr_info->ai_addrlen) < 0) {
    ESP_LOGE(MULTICAST_TAG, "sendto failed: errno %d", errno);
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

void udp_multicast_task(void *pvParameters) {
  state_handle_t state_handle = (state_handle_t)pvParameters;

  while (1) {
    xEventGroupWaitBits(state_handle->network_events,
                        STATE_NETWORK_EVENT_SOCKET_READY, pdFALSE, pdFALSE,
                        portMAX_DELAY);

    struct timeval tv = {
        .tv_sec = 2,
        .tv_usec = 0,
    };
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(state_handle->socket, &rfds);

    int s = select(state_handle->socket + 1, &rfds, NULL, NULL, &tv);
    if (s < 0) {
      ESP_LOGE(MULTICAST_TAG, "Select failed: errno %d", errno);
      continue;
    }

    ESP_LOGI(MULTICAST_TAG, "----%s----", state_handle->device_name);

    if (s > 0) {
      if (!FD_ISSET(state_handle->socket, &rfds)) {
        continue;
      }

      // ----------------↓ TMP CODE ↓----------------
      message_handle_t incoming_message;
      if (message_init(&incoming_message, MESSAGE_TYPE_UNKNOWN) != ESP_OK) {
        ESP_LOGE(MULTICAST_TAG, "Failed to initialize message");
        continue;
      }
      // ----------------↑ TMP CODE ↑----------------

      if (socket_receive_message(state_handle->socket, incoming_message) !=
          ESP_OK) {
        ESP_LOGE(MULTICAST_TAG, "Failed to receive message");
        message_free(incoming_message);
        continue;
      }

      // ----------------↓ TMP CODE ↓----------------
      switch (incoming_message->header.type) {
      case MESSAGE_TYPE_TEXT:
        ESP_LOGI(MULTICAST_TAG, "%s\n", incoming_message->text.value);
        break;
      case MESSAGE_TYPE_AUDIO:
        ESP_LOGI(MULTICAST_TAG, "audio message of length %d received\n",
                 incoming_message->header.length);
        break;
      default:
        ESP_LOGE(MULTICAST_TAG, "Unknown message type: %d",
                 incoming_message->header.type);
        break;
      }

      message_free(incoming_message);
      // ----------------↑ TMP CODE ↑----------------
    } else { // s == 0
      // ----------------↓ TMP CODE ↓----------------
      message_handle_t outgoing_message;
      char message_text[25];
      // +6 for the colon, space, "Hi!", and null terminator
      int length = (strlen(state_handle->device_name) * sizeof(char)) + 6;
      snprintf(message_text, length, "%s: Hi!", state_handle->device_name);
      if (message_init_text(&outgoing_message, message_text) != ESP_OK) {
        ESP_LOGE(MULTICAST_TAG, "Failed to initialize message");
        continue;
      }
      // ----------------↑ TMP CODE ↑----------------

      // can likely hoist a lot of this and not do it every time before sending
      struct addrinfo hints = {
          .ai_flags = AI_PASSIVE,
          .ai_socktype = SOCK_DGRAM,
          .ai_family = AF_INET,
      };
      struct addrinfo *addr_info;

      int err = getaddrinfo(CONFIG_MULTICAST_ADDR, NULL, &hints, &addr_info);
      if (err < 0) {
        ESP_LOGE(MULTICAST_TAG,
                 "getaddrinfo() failed for destination address. error: %d",
                 err);
        message_free(outgoing_message);
        continue;
      }

      if (addr_info == 0) {
        ESP_LOGE(MULTICAST_TAG, "getaddrinfo() did not return any addresses");
        freeaddrinfo(addr_info);
        message_free(outgoing_message);
        continue;
      }

      ((struct sockaddr_in *)addr_info->ai_addr)->sin_port =
          htons(CONFIG_MULTICAST_PORT);

      ESP_LOGI(MULTICAST_TAG, "Sending message to %s:%d\n",
               CONFIG_MULTICAST_ADDR, CONFIG_MULTICAST_PORT);

      if (socket_send_message(state_handle->socket, outgoing_message,
                              addr_info) != ESP_OK) {
        ESP_LOGE(MULTICAST_TAG, "Failed to send message");
      }

      // fail or not, we need to free the resources
      freeaddrinfo(addr_info);

      // ----------------↓ TMP CODE ↓----------------
      message_free(outgoing_message);
      // ----------------↑ TMP CODE ↑----------------
    }
  }
}

// ----------------
// Setup Stuff
// ----------------

esp_err_t udp_multicast_init(state_handle_t state_handle) {
  BaseType_t xReturned;

  xReturned =
      xTaskCreate(udp_multicast_task, MULTICAST_TAG,
                  STATE_TASK_STACK_DEPTH_MULTICAST, state_handle,
                  STATE_TASK_PRIORITY_MULTICAST, &state_handle->task_multicast);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast task");
    return ESP_ERR_INVALID_STATE;
  }
  if (state_handle->task_multicast == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast task");
    return ESP_ERR_NO_MEM;
  }

  xReturned = xTaskCreate(
      udp_socket_task, SOCKET_TAG, STATE_TASK_STACK_DEPTH_SOCKET, state_handle,
      STATE_TASK_PRIORITY_SOCKET, &state_handle->task_socket);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create socket task");
    return ESP_ERR_INVALID_STATE;
  }
  if (state_handle->task_socket == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create socket task");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}