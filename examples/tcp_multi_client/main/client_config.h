#pragma once
// Turns the "Multi Client Configuration" menuconfig entries into a table.

#include <stdint.h>
#include "sdkconfig.h"

struct ClientConfig {
	int         id;
	const char* host;        // never empty: falls back to CONFIG_MULTI_DEFAULT_HOST
	uint16_t    port;        // never 0: falls back to CONFIG_MULTI_DEFAULT_PORT
	const char* netif_key;   // "" = any interface
};

#define MULTI_HOST(h) (sizeof(h) > 1 ? (h) : CONFIG_MULTI_DEFAULT_HOST)
#define MULTI_PORT(p) ((p) != 0 ? (uint16_t)(p) : (uint16_t)CONFIG_MULTI_DEFAULT_PORT)

// ---- client 1 ----
#if CONFIG_MULTI_CLIENT1_IFACE_WIFI_STA
#define MULTI_CLIENT1_KEY "WIFI_STA_DEF"
#elif CONFIG_MULTI_CLIENT1_IFACE_ETH
#define MULTI_CLIENT1_KEY "ETH_DEF"
#elif CONFIG_MULTI_CLIENT1_IFACE_CUSTOM
#define MULTI_CLIENT1_KEY CONFIG_MULTI_CLIENT1_IFACE_KEY
#else
#define MULTI_CLIENT1_KEY ""
#endif

// ---- client 2 ----
#if CONFIG_MULTI_CLIENT2_IFACE_WIFI_STA
#define MULTI_CLIENT2_KEY "WIFI_STA_DEF"
#elif CONFIG_MULTI_CLIENT2_IFACE_ETH
#define MULTI_CLIENT2_KEY "ETH_DEF"
#elif CONFIG_MULTI_CLIENT2_IFACE_CUSTOM
#define MULTI_CLIENT2_KEY CONFIG_MULTI_CLIENT2_IFACE_KEY
#else
#define MULTI_CLIENT2_KEY ""
#endif

// ---- client 3 ----
#if CONFIG_MULTI_CLIENT3_IFACE_WIFI_STA
#define MULTI_CLIENT3_KEY "WIFI_STA_DEF"
#elif CONFIG_MULTI_CLIENT3_IFACE_ETH
#define MULTI_CLIENT3_KEY "ETH_DEF"
#elif CONFIG_MULTI_CLIENT3_IFACE_CUSTOM
#define MULTI_CLIENT3_KEY CONFIG_MULTI_CLIENT3_IFACE_KEY
#else
#define MULTI_CLIENT3_KEY ""
#endif

static const ClientConfig kClients[] = {
#if CONFIG_MULTI_CLIENT1_ENABLE
	{ 1, MULTI_HOST(CONFIG_MULTI_CLIENT1_HOST), MULTI_PORT(CONFIG_MULTI_CLIENT1_PORT), MULTI_CLIENT1_KEY },
#endif
#if CONFIG_MULTI_CLIENT2_ENABLE
	{ 2, MULTI_HOST(CONFIG_MULTI_CLIENT2_HOST), MULTI_PORT(CONFIG_MULTI_CLIENT2_PORT), MULTI_CLIENT2_KEY },
#endif
#if CONFIG_MULTI_CLIENT3_ENABLE
	{ 3, MULTI_HOST(CONFIG_MULTI_CLIENT3_HOST), MULTI_PORT(CONFIG_MULTI_CLIENT3_PORT), MULTI_CLIENT3_KEY },
#endif
	{ 0, nullptr, 0, nullptr }   // end marker (keeps the array non-empty)
};
