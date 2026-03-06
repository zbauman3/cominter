#pragma once

#include <stdint.h>

typedef uint8_t network_mac_address_t[6];

static const network_mac_address_t NETWORK_MESSAGE_BROADCAST_MAC_ADDRESS = {
    255, 255, 255, 255, 255, 255};