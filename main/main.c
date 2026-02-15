#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>
#include <sys/param.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "device/persistence.h"
#include "device/state.h"
#include "network/wifi.h"

static char *TAG = "MULTICAST";

static int
socket_add_ipv4_multicast_group(int sock, bool assign_source_if,
                                device_state_handle_t device_state_handle) {
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};
  int err = 0;
  inet_addr_from_ip4addr(&iaddr, &device_state_handle->ip_info->ip);
  // Configure multicast address to listen to
  err = inet_aton(CONFIG_MULTICAST_ADDR, &imreq.imr_multiaddr.s_addr);
  if (err != 1) {
    ESP_LOGE(TAG, "Configured IPV4 multicast address '%s' is invalid.",
             CONFIG_MULTICAST_ADDR);
    // Errors in the return value have to be negative
    err = -1;
    goto err;
  }
  ESP_LOGI(TAG, "Configured IPV4 Multicast address %s",
           inet_ntoa(imreq.imr_multiaddr.s_addr));
  if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr))) {
    ESP_LOGW(TAG,
             "Configured IPV4 multicast address '%s' is not a valid multicast "
             "address. This will probably not work.",
             CONFIG_MULTICAST_ADDR);
  }

  if (assign_source_if) {
    // Assign the IPv4 multicast source interface, via its IP
    // (only necessary if this socket is IPV4 only)
    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iaddr,
                     sizeof(struct in_addr));
    if (err < 0) {
      ESP_LOGE(TAG, "Failed to set IP_MULTICAST_IF. Error %d", errno);
      goto err;
    }
  }

  err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq,
                   sizeof(struct ip_mreq));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to set IP_ADD_MEMBERSHIP. Error %d", errno);
    goto err;
  }

err:
  return err;
}

static int
create_multicast_ipv4_socket(device_state_handle_t device_state_handle) {
  struct sockaddr_in saddr = {0};
  int sock = -1;
  int err = 0;

  sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket. Error %d", errno);
    return -1;
  }

  // Bind the socket to any address
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(CONFIG_MULTICAST_PORT);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to bind socket. Error %d", errno);
    goto err;
  }

  // Assign multicast TTL (set separately from normal interface TTL)
  uint8_t ttl = CONFIG_MULTICAST_TTL;
  err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to set IP_MULTICAST_TTL. Error %d", errno);
    goto err;
  }

  // this is also a listening socket, so add it to the multicast
  // group for listening...
  err = socket_add_ipv4_multicast_group(sock, true, device_state_handle);
  if (err < 0) {
    goto err;
  }

  // All set, socket is configured for sending and receiving
  return sock;

err:
  close(sock);
  return -1;
}

static void mcast_example_task(void *pvParameters) {
  device_state_handle_t device_state_handle =
      (device_state_handle_t)pvParameters;
  while (1) {
    int sock;

    sock = create_multicast_ipv4_socket(device_state_handle);
    if (sock < 0) {
      ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
    }

    if (sock < 0) {
      // Nothing to do!
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }

    // set destination multicast addresses for sending from these sockets
    struct sockaddr_in sdestv4 = {
        .sin_family = PF_INET,
        .sin_port = htons(CONFIG_MULTICAST_PORT),
    };
    // We know this inet_aton will pass because we did it above already
    inet_aton(CONFIG_MULTICAST_ADDR, &sdestv4.sin_addr.s_addr);

    // Loop waiting for UDP received, and sending UDP packets if we don't
    // see any.
    int err = 1;
    while (err > 0) {
      struct timeval tv = {
          .tv_sec = 2,
          .tv_usec = 0,
      };
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(sock, &rfds);

      int s = select(sock + 1, &rfds, NULL, NULL, &tv);
      if (s < 0) {
        ESP_LOGE(TAG, "Select failed: errno %d", errno);
        err = -1;
        break;
      } else if (s > 0) {
        if (FD_ISSET(sock, &rfds)) {
          // Incoming datagram received
          char recvbuf[48];
          char raddr_name[32] = {0};

          struct sockaddr_storage raddr; // Large enough for both IPv4 or IPv6
          socklen_t socklen = sizeof(raddr);
          int len = recvfrom(sock, recvbuf, sizeof(recvbuf) - 1, 0,
                             (struct sockaddr *)&raddr, &socklen);
          if (len < 0) {
            ESP_LOGE(TAG, "multicast recvfrom failed: errno %d", errno);
            err = -1;
            break;
          }

          // Get the sender's address as a string
          if (raddr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&raddr)->sin_addr, raddr_name,
                        sizeof(raddr_name) - 1);
          }
          ESP_LOGI(TAG, "received %d bytes from %s:", len, raddr_name);

          recvbuf[len] = 0; // Null-terminate whatever we received and treat
                            // like a string...
          ESP_LOGI(TAG, "%s", recvbuf);
        }
      } else { // s == 0
        // Timeout passed with no incoming data, so send something!
        static int send_count;
        const char sendfmt[] = "Multicast #%d sent by ESP32\n";
        char sendbuf[48];
        char addrbuf[32] = {0};
        int len = snprintf(sendbuf, sizeof(sendbuf), sendfmt, send_count++);
        if (len > sizeof(sendbuf)) {
          ESP_LOGE(TAG, "Overflowed multicast sendfmt buffer!!");
          send_count = 0;
          err = -1;
          break;
        }

        struct addrinfo hints = {
            .ai_flags = AI_PASSIVE,
            .ai_socktype = SOCK_DGRAM,
        };
        struct addrinfo *res;

        hints.ai_family = AF_INET; // For an IPv4 socket
        int err = getaddrinfo(CONFIG_MULTICAST_ADDR, NULL, &hints, &res);
        if (err < 0) {
          ESP_LOGE(
              TAG,
              "getaddrinfo() failed for IPV4 destination address. error: %d",
              err);
          break;
        }
        if (res == 0) {
          ESP_LOGE(TAG, "getaddrinfo() did not return any addresses");
          break;
        }
        ((struct sockaddr_in *)res->ai_addr)->sin_port =
            htons(CONFIG_MULTICAST_PORT);
        inet_ntoa_r(((struct sockaddr_in *)res->ai_addr)->sin_addr, addrbuf,
                    sizeof(addrbuf) - 1);
        ESP_LOGI(TAG, "Device name: %s", device_state_handle->device_name);
        ESP_LOGI(TAG, "Sending to IPV4 multicast address %s:%d...", addrbuf,
                 CONFIG_MULTICAST_PORT);
        err = sendto(sock, sendbuf, len, 0, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        if (err < 0) {
          ESP_LOGE(TAG, "IPV4 sendto failed. errno: %d", errno);
          break;
        }
      }
    }

    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    shutdown(sock, 0);
    close(sock);
  }
}

void app_main(void) {
  device_state_handle_t device_state_handle;
  ESP_ERROR_CHECK(device_state_init(&device_state_handle));

  ESP_ERROR_CHECK(persistence_init());
  ESP_ERROR_CHECK(persistence_fetch_name(device_state_handle));

  ESP_ERROR_CHECK(esp_event_loop_create_default());

  ESP_ERROR_CHECK(wifi_init(device_state_handle));

  while (device_state_handle->ip_info->ip.addr == 0) {
    ESP_LOGI(TAG, "Waiting for IP info");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  xTaskCreate(&mcast_example_task, "mcast_task", 4096, device_state_handle, 5,
              NULL);
}
