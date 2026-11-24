// Connects to a server, sends a line, logs whatever comes back, reconnects when
// the connection drops. Lines typed in the serial monitor are sent to the server
// (see console.cpp).
// Set the server in `idf.py menuconfig`:
//   "Example Configuration"            IP version, server address, port, interface
//   "Example Connection Configuration" Wi-Fi SSID and password
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "console.h"
#include "tcplib.h"
#include <string.h>

static const char* TAG = "example";



#if CONFIG_EXAMPLE_IPV6
#define SERVER_HOST CONFIG_EXAMPLE_HOST_IPV6_ADDR
#else
#define SERVER_HOST CONFIG_EXAMPLE_HOST_IPV4_ADDR
#endif
#define SERVER_PORT CONFIG_EXAMPLE_HOST_PORT



static TcpOptions clientOptions()
{
	TcpOptions o;
#if CONFIG_EXAMPLE_USER_SPECIFIED_IFACE
	o.netif_key = CONFIG_EXAMPLE_NETIF_KEY;
#endif
	return o;
}




// Talks to the server until the connection drops.
static void runConnection(TcpClient& client)
{
	char first_message[] = "Hello, I'm ESP32";
	ESP_LOGI(TAG, "sent: %s", first_message);
	client.write(first_message, strlen(first_message), 1000);

	char buf[128];
	int idle_ms = 0;
	while (client.isOpen()) {
		if (!console_send_queued(client)) return;

		size_t got = 0;
		tcp_status_t st = client.read(buf, sizeof(buf) - 1, got, 100);   // short: console stays responsive
		if (st == tcp_status_t::Ok) {
			buf[got] = '\0';
			ESP_LOGI(TAG, "rcvd: %s", buf);
			idle_ms = 0;
		}
		else if (st == tcp_status_t::Timeout) {
			idle_ms += 100;
			if (idle_ms >= 5000) {                    // nothing for 5 s: is the peer still there?
				idle_ms = 0;
				if (client.checkAlive() != tcp_status_t::Ok) return;
			}
		}
		else {
			return;   // Closed or Error
		}
	}
}



extern "C" void app_main(void)
{
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(example_connect());
	
#if CONFIG_EXAMPLE_CONNECT_WIFI
	// No Wi-Fi power save: lower latency, no missed broadcast/multicast frames
	// while dozing. Costs more power; remove for battery-powered devices.
	esp_wifi_set_ps(WIFI_PS_NONE);
#endif
	console_start();

	const TcpOptions opts = clientOptions();
	
	while (1)
	{
		ESP_LOGI(TAG, "connecting to %s port %d", SERVER_HOST, SERVER_PORT);
		TcpClient client;
		int err = 0;
		tcp_status_t st = client.connect(SERVER_HOST, SERVER_PORT, CONFIG_EXAMPLE_CONNECT_TIMEOUT_MS, opts, &err);
		if (st != tcp_status_t::Ok) {
			ESP_LOGW(TAG, "connect failed: %s (errno/code %d)", tcpStatusToString(st), err);
			vTaskDelay(pdMS_TO_TICKS(5000));
			continue;
		}
		ESP_LOGI(TAG, "connected to %s port %u ", client.remoteIpString().c_str(), client.remotePort());
		ESP_LOGI(TAG, "type a line in the monitor to send it:");

		console_set_connected(true);
		runConnection(client);
		console_set_connected(false);

		ESP_LOGI(TAG, "connection lost, retrying...");
		vTaskDelay(pdMS_TO_TICKS(2000));
	}
}
