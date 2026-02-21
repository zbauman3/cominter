// Useful docs:
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/lwip.html#bsd-sockets-api

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>
#include <sys/param.h>

#include "network/udp.h"

#define SEND_REC_BUFF_SIZE 64

static const char *BASE_TAG = "NETWORK:UDP";
static const char *SOCKET_TAG = "NETWORK:UDP:SOCKET";
static const char *MULTICAST_TAG = "NETWORK:UDP:MULTICAST";

// ----------------
// Socket Stuff
// ----------------

static esp_err_t udp_socket_create(device_state_handle_t device_state_handle) {
  if (device_state_handle->socket >= 0) {
    ESP_LOGW(SOCKET_TAG,
             "Multicast socket already created. Returning existing socket.");
    return device_state_handle->socket;
  }

  struct sockaddr_in saddr = {0};
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};
  esp_err_t ret = ESP_OK;

  // Create the socket
  device_state_handle->socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  ESP_GOTO_ON_FALSE(device_state_handle->socket >= 0, ESP_ERR_INVALID_STATE,
                    udp_multicast_socket_create_end, SOCKET_TAG,
                    "Failed to create socket: %d", errno);

  // Bind the socket to the multicast port on any address
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(CONFIG_MULTICAST_PORT);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  ESP_GOTO_ON_FALSE(bind(device_state_handle->socket, (struct sockaddr *)&saddr,
                         sizeof(struct sockaddr_in)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to bind socket: %d", errno);

  // Assign multicast TTL (set separately from normal interface TTL)
  uint8_t ttl = CONFIG_MULTICAST_TTL;
  ESP_GOTO_ON_FALSE(setsockopt(device_state_handle->socket, IPPROTO_IP,
                               IP_MULTICAST_TTL, &ttl, sizeof(uint8_t)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_MULTICAST_TTL: %d", errno);

  // Configure the multicast source interface address
  inet_addr_from_ip4addr(&iaddr, &device_state_handle->ip_info->ip);

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
  ESP_GOTO_ON_FALSE(setsockopt(device_state_handle->socket, IPPROTO_IP,
                               IP_MULTICAST_IF, &iaddr,
                               sizeof(struct in_addr)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_MULTICAST_IF: %d", errno);

  // Add the multicast group to the socket
  ESP_GOTO_ON_FALSE(setsockopt(device_state_handle->socket, IPPROTO_IP,
                               IP_ADD_MEMBERSHIP, &imreq,
                               sizeof(struct ip_mreq)) >= 0,
                    ESP_ERR_INVALID_STATE, udp_multicast_socket_create_end,
                    SOCKET_TAG, "Failed to set IP_ADD_MEMBERSHIP: %d", errno);

udp_multicast_socket_create_end:
  if (ret != ESP_OK) {
    if (device_state_handle->socket >= 0) {
      close(device_state_handle->socket);
      device_state_handle->socket = -1;
    }
  }

  return ret;
}

void udp_socket_task(void *pvParameters) {
  device_state_handle_t device_state_handle =
      (device_state_handle_t)pvParameters;
  esp_err_t create_status;

  while (1) {
    xEventGroupWaitBits(device_state_handle->network_events,
                        STATE_NETWORK_EVENT_GOT_NEW_IP, pdTRUE, pdTRUE,
                        portMAX_DELAY);
    ESP_LOGI(SOCKET_TAG, "Got new IP, creating socket...");

    udp_socket_close(device_state_handle);

    while (device_state_handle->socket < 0) {
      create_status = ESP_OK;
      create_status = udp_socket_create(device_state_handle);

      if (create_status != ESP_OK || device_state_handle->socket < 0) {
        ESP_LOGE(SOCKET_TAG, "Failed to create multicast socket. Retrying...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
      } else {
        ESP_LOGI(SOCKET_TAG, "Socket created successfully");
        xEventGroupSetBits(device_state_handle->network_events,
                           STATE_NETWORK_EVENT_SOCKET_READY);
      }
    }
  }
}

void udp_socket_close(device_state_handle_t device_state_handle) {
  if (device_state_handle->socket >= 0) {
    shutdown(device_state_handle->socket, SHUT_RDWR);
    close(device_state_handle->socket);
  }
  xEventGroupClearBits(device_state_handle->network_events,
                       STATE_NETWORK_EVENT_SOCKET_READY);
  device_state_handle->socket = -1;
}

// ----------------
// Multicast Stuff
// ----------------

void udp_multicast_task(void *pvParameters) {
  device_state_handle_t device_state_handle =
      (device_state_handle_t)pvParameters;

  while (1) {
    xEventGroupWaitBits(device_state_handle->network_events,
                        STATE_NETWORK_EVENT_SOCKET_READY, pdFALSE, pdFALSE,
                        portMAX_DELAY);

    int err = 1;
    struct timeval tv = {
        .tv_sec = 2,
        .tv_usec = 0,
    };
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(device_state_handle->socket, &rfds);

    int s = select(device_state_handle->socket + 1, &rfds, NULL, NULL, &tv);
    if (s < 0) {
      ESP_LOGE(MULTICAST_TAG, "Select failed: errno %d", errno);
      continue;
    }

    ESP_LOGI(MULTICAST_TAG, "----%s----", device_state_handle->device_name);

    if (s > 0) {
      if (FD_ISSET(device_state_handle->socket, &rfds)) {

        // Incoming datagram received
        char recvbuf[SEND_REC_BUFF_SIZE];
        char raddr_name[32] = {0};

        struct sockaddr_storage raddr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(raddr);
        int len =
            recvfrom(device_state_handle->socket, recvbuf, sizeof(recvbuf) - 1,
                     0, (struct sockaddr *)&raddr, &socklen);
        if (len < 0) {
          ESP_LOGE(MULTICAST_TAG, "multicast recvfrom failed: errno %d", errno);
          continue;
        }

        // Get the sender's address as a string
        if (raddr.ss_family == PF_INET) {
          inet_ntoa_r(((struct sockaddr_in *)&raddr)->sin_addr, raddr_name,
                      sizeof(raddr_name) - 1);
        }
        ESP_LOGI(MULTICAST_TAG, "received %d bytes from %s:", len, raddr_name);

        recvbuf[len] = 0; // Null-terminate whatever we received and treat
                          // like a string...
        ESP_LOGI(MULTICAST_TAG, "%s\n", recvbuf);
      }
    } else { // s == 0
      // Timeout passed with no incoming data, so send something!
      static int send_count;
      const char sendfmt[] = "Multicast #%d sent by %s";
      char sendbuf[SEND_REC_BUFF_SIZE];
      char addrbuf[32] = {0};
      int len = snprintf(sendbuf, sizeof(sendbuf), sendfmt, send_count++,
                         device_state_handle->device_name);
      if (len > sizeof(sendbuf)) {
        ESP_LOGE(MULTICAST_TAG, "Overflowed multicast sendfmt buffer!!");
        send_count = 0;
        continue;
      }

      struct addrinfo hints = {
          .ai_flags = AI_PASSIVE,
          .ai_socktype = SOCK_DGRAM,
      };
      struct addrinfo *res;

      hints.ai_family = AF_INET; // For an IPv4 socket
      err = getaddrinfo(CONFIG_MULTICAST_ADDR, NULL, &hints, &res);
      if (err < 0) {
        ESP_LOGE(MULTICAST_TAG,
                 "getaddrinfo() failed for IPV4 destination address. error: %d",
                 err);
        continue;
      }
      if (res == 0) {
        ESP_LOGE(MULTICAST_TAG, "getaddrinfo() did not return any addresses");
        continue;
      }
      ((struct sockaddr_in *)res->ai_addr)->sin_port =
          htons(CONFIG_MULTICAST_PORT);
      inet_ntoa_r(((struct sockaddr_in *)res->ai_addr)->sin_addr, addrbuf,
                  sizeof(addrbuf) - 1);
      ESP_LOGI(MULTICAST_TAG, "Sending to IPV4 multicast address %s:%d...\n",
               addrbuf, CONFIG_MULTICAST_PORT);
      err = sendto(device_state_handle->socket, sendbuf, len, 0, res->ai_addr,
                   res->ai_addrlen);
      freeaddrinfo(res);
      if (err < 0) {
        ESP_LOGE(MULTICAST_TAG, "IPV4 sendto failed. errno: %d", errno);
        continue;
      }
    }
  }
}

// ----------------
// Setup Stuff
// ----------------

esp_err_t udp_multicast_init(device_state_handle_t device_state_handle) {
  BaseType_t xReturned;

  xReturned = xTaskCreate(udp_multicast_task, MULTICAST_TAG,
                          STATE_TASK_STACK_DEPTH_MULTICAST, device_state_handle,
                          STATE_TASK_PRIORITY_MULTICAST,
                          &device_state_handle->task_multicast);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast task");
    return ESP_ERR_INVALID_STATE;
  }
  if (device_state_handle->task_multicast == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create multicast task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(BASE_TAG, "Created multicast task");

  xReturned =
      xTaskCreate(udp_socket_task, SOCKET_TAG, STATE_TASK_STACK_DEPTH_SOCKET,
                  device_state_handle, STATE_TASK_PRIORITY_SOCKET,
                  &device_state_handle->task_socket);

  if (xReturned != pdPASS) {
    ESP_LOGE(BASE_TAG, "Failed to create socket task");
    return ESP_ERR_INVALID_STATE;
  }
  if (device_state_handle->task_socket == NULL) {
    ESP_LOGE(BASE_TAG, "Failed to create socket task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(BASE_TAG, "Created socket task");

  return ESP_OK;
}